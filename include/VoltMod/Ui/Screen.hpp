#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <memory>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One spawned Panorama layout, from @ref ScreenManager: shared by everyone, or owned by one player.
 *
 * A shared screen writes for @ref EveryoneSlot or one player. A player screen is networked to its
 * owner alone and stays visible while they are dead or spectating; its writes take the owner's slot
 * or @ref EveryoneSlot.
 *
 * Writes never spawn: call @ref EnsureSpawned before a redraw. It also recovers after a map change
 * removed the entity. Unchanged values are not re-sent, so redrawing everything is cheap. Destroying
 * the screen removes the entity. Does nothing unless @ref ScreenManager::Available succeeds.
 */
class Screen
{
public:
    /** An empty screen: every write fails with Error::NotFound. */
    Screen();
    ~Screen();

    Screen(Screen&&) noexcept;
    /** Removes what this screen held before taking the other one's entity. */
    Screen& operator=(Screen&&) noexcept;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    /** Whether the entity exists right now. */
    explicit operator bool() const;

    /** Spawn or respawn until @p slot can be written to. False logs why once per attempt. */
    bool EnsureSpawned(int slot);

    /** Set the text a `text="{s:variable}"` Label shows, for @p slot or @ref EveryoneSlot. */
    Status SetText(int slot, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on the element with @p elementId. */
    Status SetClass(int slot, std::string_view elementId, std::string_view className, bool on);

    /** Add or remove `Hidden`, the class every block and layout root collapses on. */
    Status SetHidden(int slot, std::string_view elementId, bool hidden);

    /** Give @p slot a mouse cursor over the layout. Nothing in it is clickable without one. */
    Status ShowCursor(int slot, bool shown);

    /** Remove the entity now instead of at destruction. The next @ref EnsureSpawned spawns a fresh one. */
    void Remove();

private:
    friend class ScreenManager;

    explicit Screen(std::unique_ptr<ScreenEntity> entity);

    static Error Empty();

    std::unique_ptr<ScreenEntity> _entity;
};

}  // namespace VoltMod
