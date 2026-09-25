#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <memory>
#include <span>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A spawned Panorama layout created by @ref ScreenManager.
 *
 * Shared screens write for @ref EveryoneSlot or a selected player. Player screens are networked only
 * to their owner and remain visible while the owner is dead or spectating.
 *
 * Writes do not spawn the entity. Call @ref EnsureSpawned before drawing; it also respawns after a
 * map change. Unchanged values are not resent, and a failed write is logged once per slot and retried
 * on the next change. Destruction removes the entity. Operations are inert unless
 * @ref ScreenManager::Available succeeds.
 */
class Screen
{
public:
    /** Construct an empty screen. Writes do nothing until an entity is assigned. */
    Screen();
    ~Screen();

    Screen(Screen&&) noexcept;
    /** Replace the entity, removing the one previously held by this screen. */
    Screen& operator=(Screen&&) noexcept;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    explicit operator bool() const;

    /** Spawn or respawn until @p slot can be written to. False logs why once per attempt. */
    bool EnsureSpawned(int slot);

    /** Set the text a `text="{s:variable}"` Label shows, for @p slot or @ref EveryoneSlot. */
    void SetText(int slot, std::string_view variable, std::string_view value);

    void SetClass(int slot, std::string_view elementId, std::string_view className, bool on);

    void SetHidden(int slot, std::string_view elementId, bool hidden);

    /** Show @p name in the icon set @p elementId: `icon-set--<name>` on, every other of @p names off.
     *  @p names is the generated header's `IconSetNames`. */
    void ShowIcon(int slot, std::string_view elementId, std::span<const std::string_view> names, std::string_view name);

    void ShowCursor(int slot, bool shown);

    /** Remove the entity now. The next @ref EnsureSpawned creates a new one. */
    void Remove();

private:
    friend class ScreenManager;

    explicit Screen(std::unique_ptr<ScreenEntity> entity);

    std::unique_ptr<ScreenEntity> _entity;
};

}  // namespace VoltMod
