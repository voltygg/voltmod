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

/** SourceHook removal function. */
using VtableHookRemover = void (*)(int hookId);

/**
 * Move-only owner for manual SourceHook registrations. Destruction removes all handlers. Hook
 * pairs install atomically, and removal remains safe after instance destruction.
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

    /** Install a pre-hook, post-hook, or both on a class vtable. */
    template <class Decl, class Sig, class Self, class Pre, class Post>
    static Result<VtableHook> OnVTable(std::string_view what, const VHookBinding<Sig>& binding, Self* self, Pre pre,
                                       Post post, void* sampleInstance = nullptr);

    /** Install one handler on one instance. Reset the hook before the instance is destroyed. */
    template <class Decl, class Self, class Handler>
    static Result<VtableHook> OnInstance(std::string_view what, void* instance, int index, Self* self, Handler handler,
                                         bool post);

    explicit operator bool() const noexcept { return _preId != 0 || _postId != 0; }

    /** Remove installed handlers. Safe to call repeatedly and after instance destruction. */
    void Reset() noexcept;

    /** SDK-free install policy used by tests. @p remove must outlive the returned hook. */
    static Result<VtableHook> Install(std::string_view what, bool wantPre, bool wantPost,
                                      const std::function<int(bool post)>& add, VtableHookRemover remove);

private:
    int _preId = 0;
    int _postId = 0;
    VtableHookRemover _remove = nullptr;
};

template <class Decl, class Sig, class Self, class Pre, class Post>
Result<VtableHook> VtableHook::OnVTable(std::string_view what, const VHookBinding<Sig>& binding, Self* self, Pre pre,
                                        Post post, void* sampleInstance)
{
    constexpr bool wantPre = !std::is_null_pointer_v<Pre>;
    constexpr bool wantPost = !std::is_null_pointer_v<Post>;
    static_assert(wantPre || wantPost, "a VtableHook with neither side would install nothing");

    const VTableRef& table = binding.Table;
    const int index = binding.Method.Index();
    if (index < 0)
        return std::unexpected(Error::Unsupported(std::format("the {} vtable index did not bind", what)));
    if (!table)
        return std::unexpected(Error::Engine(std::format("the {} class vtable did not bind", what)));

    // A live instance detects a stale gamedata class name without blocking early installation.
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

    return Install(
        what, !post, post, [&](bool) { return Decl::AddInstance(instance, self, handler, post); }, &Decl::Remove);
}

}  // namespace VoltMod

/** Traits emitted by each `VOLTMOD_VHOOK*` declaration. */
#define VOLTMOD_VHOOK_TRAITS(Name)                                               \
    struct Name##Hook                                                            \
    {                                                                            \
        /** Current SourceHook descriptor slot. */                               \
        static inline int Configured = -1;                                       \
                                                                                 \
        /** Keep a live descriptor on its configured slot. */                    \
        static void Reconfigure(int index)                                       \
        {                                                                        \
            if (Configured == index)                                             \
                return;                                                          \
            SH_MANUALHOOK_RECONFIGURE(Name, index, 0, 0);                        \
            Configured = index;                                                  \
        }                                                                        \
                                                                                 \
        template <class Self, class Fn>                                          \
        static int AddVTable(void* table, Self* self, Fn fn, bool post)          \
        {                                                                        \
            return SH_ADD_MANUALDVPHOOK(Name, table, SH_MEMBER(self, fn), post); \
        }                                                                        \
                                                                                 \
        template <class Self, class Fn>                                          \
        static int AddInstance(void* instance, Self* self, Fn fn, bool post)     \
        {                                                                        \
            return SH_ADD_MANUALHOOK(Name, instance, SH_MEMBER(self, fn), post); \
        }                                                                        \
                                                                                 \
        static void Remove(int hookId) { SH_REMOVE_HOOK_ID(hookId); }            \
    }

/**
 * Declare one manual virtual hook at namespace scope. Use `_VOID` for void returns. Include
 * `MetamodGlobals.hpp` first, use a unique name, and match the engine signature.
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
