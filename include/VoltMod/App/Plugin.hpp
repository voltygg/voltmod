#pragma once

#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Everything a plugin owns for one load cycle.
 *
 * The framework constructs the derived class with a live @ref Runtime, calls @ref Load, and
 * destroys it before that runtime. Release plugin-owned resources in member destructors.
 */
class Plugin
{
public:
    explicit Plugin(VoltMod::Runtime& runtime) : Runtime(runtime) {}
    virtual ~Plugin() = default;

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    /** Framework services for this plugin's load cycle. */
    VoltMod::Runtime& Runtime;

    /** Load configuration and start the plugin. Returning false aborts the load. */
    virtual bool Load() = 0;

    /** Called at each map start after game-event listeners are attached. */
    virtual void OnServerStartup(std::string_view /*mapName*/) {}

    /**
     * @brief A player sent a `say` or `say_team` message.
     *
     * The default consumes pending menu input, then dispatches registered `!` and `.` commands.
     * An override replaces both behaviors.
     */
    virtual bool OnPlayerChat(Player* player, std::string_view message, bool teamChat);
};

}  // namespace VoltMod
