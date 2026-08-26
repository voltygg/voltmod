#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <format>
#include <functional>
#include <string_view>
#include <type_traits>

namespace VoltMod
{

/**
 * @brief Removes one SourceHook hook by its id.
 *
 * A plain function pointer rather than a `std::function`: every @ref VtableHook stores one and
 * @ref Teleport keeps `MaxPlayers` of them, so this sits on `sizeof(Runtime)`'s budget.
 * @ref VOLTMOD_VHOOK1 and friends generate the real one; a test injects its own.
 */
using VtableHookRemover = void (*)(int hookId);

/**
 * @brief One installed manual SourceHook hook - a pre/post pair on a class vtable, or a single
 * side on one instance - owned as a value and removed when it goes away.
 *
 * The declaration half lives in a @ref VOLTMOD_VHOOK1 "VOLTMOD_VHOOK" macro at file scope; this is
 * the install half. Together they replace the reconfigure/add/id-bookkeeping that every hook
 * service used to repeat:
 *
 * @code
 * // exactly once per translation unit, at namespace scope
 * VOLTMOD_VHOOK1(MyPlugin_RunCommand, void*, void*);
 *
 * auto hook = VtableHook::OnVTable<MyPlugin_RunCommandHook>(
 *     "MyPlugin RunCommand", bindings.MovementServices, bindings.RunCommand.Index(), this,
 *     &MyPlugin::Hook_Pre, &MyPlugin::Hook_Post);
 * if (!hook)
 *     Log::Warn("{}", hook.error().Detail);   // nothing was installed
 * else
 *     _hook = std::move(*hook);               // dropping _hook removes both sides
 * @endcode
 *
 * **Pair-or-nothing.** When both sides are asked for and either is refused, nothing stays
 * installed and the error names the side that failed. Half a pair is worse than none: the
 * surviving post would run against state the pre it brackets never established, and a handler
 * cannot see that its counterpart is missing.
 *
 * **Removal is by id.** SourceHook resolves the id from what it recorded at add time and never
 * dereferences the hooked object, so @ref Reset is safe after a map change has freed every
 * instance the hook was bound to.
 *
 * Move-only, and moving transfers the ids: the moved-from hook removes nothing.
 */
class VtableHook
{
public:
    VtableHook() = default;
    ~VtableHook();
    VtableHook(VtableHook&& other) noexcept;
    VtableHook& operator=(VtableHook&& other) noexcept;
    VtableHook(const VtableHook&) = delete;
    VtableHook& operator=(const VtableHook&) = delete;

    /**
     * Point @p Decl's hook at @p index and add @p pre and/or @p post on @p table (a DVP hook, so
     * it covers every instance of the class and can install with none alive).
     *
     * @param Decl the `Name##Hook` traits type a `VOLTMOD_VHOOK*` declaration emitted.
     * @param what what the hook is, for the install log line and every error's `Detail`.
     * @param table the class vtable, from @ref Bindings.
     * @param index the vtable slot, from `VFn::Index()`; a negative one is Unsupported.
     * @param self the object whose member functions handle the calls.
     * @param pre pre-handler member pointer, or `nullptr` for post only.
     * @param post post-handler member pointer, or `nullptr` for pre only.
     * @param sampleInstance optional live instance whose vptr is compared against @p table; a
     *        mismatch means the gamedata class name drifted and only warns, since installing must
     *        still work with no instance alive.
     */
    template <class Decl, class Self, class Pre, class Post>
    static Result<VtableHook> OnVTable(std::string_view what, const VTableRef& table, int index, Self* self, Pre pre,
                                       Post post, void* sampleInstance = nullptr);

    /**
     * Point @p Decl's hook at @p index and add one side on @p instance alone.
     *
     * The handler then runs only for that object, so a caller binding many of them gets exactly
     * one call per event however many are bound. @p instance must outlive the returned hook, or
     * be released through @ref Reset before it dies.
     */
    template <class Decl, class Self, class Handler>
    static Result<VtableHook> OnInstance(std::string_view what, void* instance, int index, Self* self, Handler handler,
                                         bool post);

    /** Whether anything is installed. */
    explicit operator bool() const noexcept { return _preId != 0 || _postId != 0; }

    /** Remove whatever is installed. Idempotent, and safe after the hooked instances are gone. */
    void Reset() noexcept;

    /**
     * The pair-or-nothing policy itself, with SourceHook injected.
     *
     * @ref OnVTable and @ref OnInstance are the callers that matter; this is public because it is
     * the seam that lets the policy be unit-tested without the SDK. @p add returns the hook id for
     * one side, or 0 when SourceHook refused it, and is called at most once per requested side -
     * post is not even attempted once a requested pre was refused. @p remove is stored in the
     * result, so it must outlive it (a free function, never a capturing lambda).
     */
    static Result<VtableHook> Install(std::string_view what, bool wantPre, bool wantPost,
                                      const std::function<int(bool post)>& add, VtableHookRemover remove);

private:
    int _preId = 0;
    int _postId = 0;
    VtableHookRemover _remove = nullptr;
};

template <class Decl, class Self, class Pre, class Post>
Result<VtableHook> VtableHook::OnVTable(std::string_view what, const VTableRef& table, int index, Self* self, Pre pre,
                                        Post post, void* sampleInstance)
{
    constexpr bool wantPre = !std::is_null_pointer_v<Pre>;
    constexpr bool wantPost = !std::is_null_pointer_v<Post>;
    static_assert(wantPre || wantPost, "a VtableHook with neither side would install nothing");

    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!table)
        return std::unexpected(Error::Engine(std::format("the {} class vtable did not bind", what)));

    // The class name drifts with game updates the way the index does, and nothing else here would
    // notice: a wrong name resolves to another class's table and the hook then never fires. A live
    // instance's own vptr is the ground truth to check against - but installing must still work
    // from OnLoad with nothing alive, so a mismatch only warns.
    if (sampleInstance && *static_cast<void**>(sampleInstance) != table.Table())
        Log::Warn("{}: a live instance's vtable differs from {}; wrong class name?", what, table.Class());

    Decl::Reconfigure(index);

    void* vtable = table.Table();
    auto add = [&](bool addPost) -> int {
        if (addPost)
        {
            if constexpr (wantPost)
                return Decl::AddVTable(vtable, self, post, true);
            else
                return 0;
        }
        if constexpr (wantPre)
            return Decl::AddVTable(vtable, self, pre, false);
        else
            return 0;
    };

    auto hook = Install(what, wantPre, wantPost, add, &Decl::Remove);
    if (hook)
        Log::Info("{} hook installed on {} vtable (index {}).", what, table.Class(), index);
    return hook;
}

template <class Decl, class Self, class Handler>
Result<VtableHook> VtableHook::OnInstance(std::string_view what, void* instance, int index, Self* self, Handler handler,
                                          bool post)
{
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!instance)
        return std::unexpected(Error::Invalid(std::format("no instance to bind the {} hook to", what)));

    Decl::Reconfigure(index);

    // Per-instance binding is one-sided; which side it is comes from the caller.
    return Install(
        what, !post, post, [&](bool) { return Decl::AddInstance(instance, self, handler, post); }, &Decl::Remove);
}

}  // namespace VoltMod

/**
 * @brief The body every `VOLTMOD_VHOOK*` declaration shares - not called directly.
 *
 * Emits `Name##Hook`, the traits type @ref VoltMod::VtableHook drives the SourceHook macros
 * through. Everything a macro name has to be a literal token for lives here, so the install code
 * itself is an ordinary template that never sees one.
 */
#define VOLTMOD_VHOOK_TRAITS(Name)                                                                    \
    struct Name##Hook                                                                                 \
    {                                                                                                 \
        /** The index the file-static descriptor currently points at; -1 before the first install. */ \
        static inline int Configured = -1;                                                            \
                                                                                                      \
        /** SH_MANUALHOOK_RECONFIGURE mutates the file-static SH_DECL_MANUALHOOK created, which would \
         *  break every hook already added through it. Gamedata resolves once per process, so the     \
         *  repeat call is always the same index and is skipped here rather than repointing a live    \
         *  hook. */                                                                                  \
        static void Reconfigure(int index)                                                            \
        {                                                                                             \
            if (Configured == index)                                                                  \
                return;                                                                               \
            SH_MANUALHOOK_RECONFIGURE(Name, index, 0, 0);                                             \
            Configured = index;                                                                       \
        }                                                                                             \
                                                                                                      \
        /** Add one side on a class vtable; 0 when SourceHook refused it. */                          \
        template <class Self, class Fn>                                                               \
        static int AddVTable(void* table, Self* self, Fn fn, bool post)                               \
        {                                                                                             \
            return SH_ADD_MANUALDVPHOOK(Name, table, SH_MEMBER(self, fn), post);                      \
        }                                                                                             \
                                                                                                      \
        /** Add one side on a single instance; 0 when SourceHook refused it. */                       \
        template <class Self, class Fn>                                                               \
        static int AddInstance(void* instance, Self* self, Fn fn, bool post)                          \
        {                                                                                             \
            return SH_ADD_MANUALHOOK(Name, instance, SH_MEMBER(self, fn), post);                      \
        }                                                                                             \
                                                                                                      \
        static void Remove(int hookId) { SH_REMOVE_HOOK_ID(hookId); }                                 \
    }

/**
 * @brief Declare one hooked virtual function, at namespace scope in the .cpp that owns the hook.
 *
 * `VOLTMOD_VHOOK<arity>(Name, Ret, params...)` for a value return and
 * `VOLTMOD_VHOOK<arity>_VOID(Name, params...)` for `void`, arities 0 to 4. Each emits SourceHook's
 * manual-hook declaration plus the `Name##Hook` traits type @ref VoltMod::VtableHook::OnVTable and
 * @ref VoltMod::VtableHook::OnInstance take as their first template argument.
 *
 * The .cpp must include the SourceHook globals (`<VoltMod/Engine/MetamodGlobals.hpp>`) before
 * using one; this header deliberately does not, so it stays free of the SDK.
 *
 * **One hooked vfunc per translation unit.** The declaration expands to namespace-scope
 * definitions and a file-static descriptor that `Reconfigure` mutates, so a second TU declaring
 * the same `Name` is a duplicate symbol, and one `Name` may never stand for two vfuncs.
 *
 * Spell the signature the way the engine declares it. A pre/post observer never touches the
 * parameters or the return, so `void*` stands in wherever the type is one no public header can
 * name.
 *
 * @code
 * // void* CPlayer_MovementServices::RunCommand(CUserCmd*)
 * VOLTMOD_VHOOK1(VoltMod_MovementRunCommand, void*, void*);
 * // void CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*)
 * VOLTMOD_VHOOK3_VOID(VoltMod_EntityTeleport, const Vector*, const QAngle*, const Vector*);
 * @endcode
 */
#define VOLTMOD_VHOOK0(Name, Ret)            \
    SH_DECL_MANUALHOOK0(Name, 0, 0, 0, Ret); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK1(Name, Ret, A1)            \
    SH_DECL_MANUALHOOK1(Name, 0, 0, 0, Ret, A1); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK2(Name, Ret, A1, A2)            \
    SH_DECL_MANUALHOOK2(Name, 0, 0, 0, Ret, A1, A2); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK3(Name, Ret, A1, A2, A3)            \
    SH_DECL_MANUALHOOK3(Name, 0, 0, 0, Ret, A1, A2, A3); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK4(Name, Ret, A1, A2, A3, A4)            \
    SH_DECL_MANUALHOOK4(Name, 0, 0, 0, Ret, A1, A2, A3, A4); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK0_VOID(Name)            \
    SH_DECL_MANUALHOOK0_void(Name, 0, 0, 0); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK1_VOID(Name, A1)            \
    SH_DECL_MANUALHOOK1_void(Name, 0, 0, 0, A1); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK2_VOID(Name, A1, A2)            \
    SH_DECL_MANUALHOOK2_void(Name, 0, 0, 0, A1, A2); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK3_VOID(Name, A1, A2, A3)            \
    SH_DECL_MANUALHOOK3_void(Name, 0, 0, 0, A1, A2, A3); \
    VOLTMOD_VHOOK_TRAITS(Name)

#define VOLTMOD_VHOOK4_VOID(Name, A1, A2, A3, A4)            \
    SH_DECL_MANUALHOOK4_void(Name, 0, 0, 0, A1, A2, A3, A4); \
    VOLTMOD_VHOOK_TRAITS(Name)
