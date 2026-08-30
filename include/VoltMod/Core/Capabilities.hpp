#pragma once

#include <VoltMod/Core/EnumNames.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Features whose availability depends on engine state or gamedata. */
enum class Capability : uint16_t
{
    Entities,     ///< CGameEntitySystem and everything reached through it.
    EntityOps,    ///< Creating and spawning entities. Other entity operations guard themselves.
    GameEvents,   ///< IGameEventManager2: game event listeners and center HTML.
    Movement,     ///< The RunCommand hook and its usercmd feed.
    Teleport,     ///< CBaseEntity::Teleport, both the hook and the direct call.
    Transmit,     ///< Per-recipient entity transmit filtering.
    ClientCvars,  ///< Asking a connected client what one of its own convars is set to.
    Precache,     ///< Adding resources to the session manifest.
    Vote,         ///< The game's own yes/no vote panel.
    Items,        ///< Giving and stripping weapons through CCSPlayer_ItemServices.
    Menus,        ///< The rendered player menu.
    Http,         ///< The HTTP client and its worker pool.
    CustomUi,     ///< Driving a custom_hud_layout: dialog variables, classes, input capture.
    UiClicks,     ///< Receiving Button presses from a custom HUD layout.
    Addons        ///< Telling connecting clients which workshop addons to download.
};

/**
 * Availability and failure reasons for optional framework features.
 * Runtime settles these values during startup.
 * Disabled services remain safe to call.
 */
class Capabilities
{
public:
    /** Whether the capability is usable for this load. */
    bool Has(Capability capability) const noexcept;

    /** Failure reason, or an empty view when enabled. The view borrows this object. */
    std::string_view Reason(Capability capability) const noexcept;

    /** One-line availability summary for logs. */
    std::string Summary() const;

    /** Framework-only update. @p reason is ignored when @p ok is true. */
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
