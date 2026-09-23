#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <VoltMod/Ui/Screen.hpp>
#include <memory>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Create @ref Screen objects and report their button presses.
 *
 * @p layout must be a bare name (`"welcome"`) or a resource name under
 * `panorama/layout/custom_game/` with its source `.xml` extension. Other forms are rejected before
 * reaching the client.
 */
class ScreenManager
{
public:
    /** Constructor dependencies must outlive this service. */
    ScreenManager(EntitySystem& entities, const Bindings& bindings, Interfaces& interfaces, SlotEvents& slots,
                  Scheduler& scheduler, Visibility& visibility);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    /** Return the reason screens are unavailable, or success when all required bindings exist. */
    Status Available() const;

    /** A screen every player receives. Spawned on its first @ref Screen::EnsureSpawned. */
    Result<Screen> Shared(std::string_view layout);

    /**
     * A screen only @p slot receives, removed when the slot changes hands. This requires
     * @ref Visibility::Available because otherwise the entity would reach everyone.
     */
    Result<Screen> ForPlayer(std::string_view layout, int slot);

    /**
     * Reports every button press from every layout. Filter on @ref ButtonPress::ButtonId.
     *
     * The hook installs for the first subscription and is removed after the last. Because only a
     * connected client exposes the target vtable, an empty server defers installation until connect.
     * Subscriptions are refused with an error when the hook cannot bind.
     */
    Event<const ButtonPress&> Pressed;

private:
    Result<Screen> Create(std::string_view layout, int owner);

    EntitySystem& _entities;
    const Bindings& _bindings;
    SlotEvents& _slots;
    Visibility& _visibility;
    /** Declared after @ref Pressed so the hook is destroyed before the event. */
    std::unique_ptr<ButtonPressHook> _hook;
};

}  // namespace VoltMod
