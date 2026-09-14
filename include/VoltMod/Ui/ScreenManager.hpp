#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <VoltMod/Ui/Screen.hpp>
#include <memory>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Creates @ref Screen objects and reports the button presses coming back from them.
 *
 * @p layout is a bare name (`"welcome"`) or a full resource name under `panorama/layout/custom_game/`
 * with its source `.xml` extension; anything else is refused here, since the client would only
 * reject it silently.
 */
class ScreenManager
{
public:
    /** All must outlive this service; the Runtime declares them above it. */
    ScreenManager(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
                  SlotEvents& slots, Scheduler& scheduler, Visibility& visibility);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    /** A screen every player receives. Spawned on its first @ref Screen::EnsureSpawned. */
    Result<Screen> Shared(std::string_view layout);

    /**
     * A screen only @p slot receives, removed when the slot changes hands. Refused while
     * @ref Capability::Visibility is off, since the entity would then reach everyone.
     */
    Result<Screen> ForPlayer(std::string_view layout, int slot);

    /**
     * Every button press, from every layout. Filter on @ref ButtonPress::ButtonId.
     *
     * The hook installs on the first subscription and is removed with the last. It sits in a vtable
     * only a connected client exposes, so subscribing on an empty server hooks on the next connect.
     * Refused after saying why when @ref Capability::ButtonPresses is off.
     */
    Event<const ButtonPress&> Pressed;

private:
    Result<Screen> Create(std::string_view layout, int owner);

    EntitySystem& _entities;
    EntityOps& _ops;
    SlotEvents& _slots;
    Visibility& _visibility;
    /** Declared after @ref Pressed so the hook is gone before the event it raises into. */
    std::unique_ptr<ButtonPressHook> _hook;
};

}  // namespace VoltMod
