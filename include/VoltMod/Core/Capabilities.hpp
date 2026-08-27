#pragma once

#include <VoltMod/Core/EnumNames.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief What this load of the framework can actually do.
 *
 * One enumerator per feature whose availability depends on something outside the plugin's control
 * - a gamedata entry, an engine interface, a vtable that resolved. The names are what
 * @ref Capabilities::Summary and the `capabilities` status section print, so they are spelled the
 * way they should read.
 */
enum class Capability : uint16_t
{
    Schema,       ///< Entity field offsets from the engine's schema system.
    Entities,     ///< CGameEntitySystem and everything reached through it.
    EntityOps,    ///< Creating, spawning, inputting and removing entities.
    GameEvents,   ///< IGameEventManager2: game event listeners and center HTML.
    Movement,     ///< The RunCommand hook and its usercmd feed.
    Teleport,     ///< CBaseEntity::Teleport, both the hook and the direct call.
    Transmit,     ///< Per-recipient entity transmit filtering.
    ClientCvars,  ///< Asking a connected client what one of its own convars is set to.
    Precache,     ///< Adding resources to the session manifest.
    Vote,         ///< The game's own yes/no vote panel.
    Items,        ///< Giving and stripping weapons through CCSPlayer_ItemServices.
    Menus,        ///< The rendered player menu.
    Http          ///< The HTTP client and its worker pool.
};

/**
 * @brief Per-capability availability, with the reason a missing one is missing.
 *
 * @ref Runtime::Start records every entry as it runs the matching setup, so a plugin's OnLoad and
 * anything after it reads a settled picture. A capability that is off means its service is inert:
 * calling into it is safe and returns @ref ErrorCode::NotReady, an empty Subscription, or nothing
 * - never a crash.
 *
 * Everything starts unavailable, so a capability nobody recorded reads as off rather than as a
 * promise that was never checked.
 *
 * @code
 * if (!runtime.Capabilities.Has(Capability::Movement))
 *     Log::Warn("no movement feed: {}", runtime.Capabilities.Reason(Capability::Movement));
 * @endcode
 */
class Capabilities
{
public:
    /** Whether @p capability is usable this load. */
    bool Has(Capability capability) const noexcept;

    /** Why @p capability is off, or an empty view when it is on. Borrows this object's storage. */
    std::string_view Reason(Capability capability) const noexcept;

    /** "12/14 ok; Movement: RunCommand vtable unresolved; Damage: ..." - one line, for the log. */
    std::string Summary() const;

    /**
     * Record @p capability's outcome. **Framework only**: `Runtime::Start` and `Bindings::Bind`
     * own every call, so a plugin that sets one is lying to every other plugin that reads it.
     * Not private only because `Bindings` reaches it through file-local helpers.
     *
     * @p reason is ignored when @p ok is true.
     */
    void Set(Capability capability, bool ok, std::string reason = {});

private:
    struct Entry
    {
        bool Ok = false;
        std::string Reason = "not initialized";
    };

    std::array<Entry, EnumCount<Capability>> _entries;
};

}  // namespace VoltMod
