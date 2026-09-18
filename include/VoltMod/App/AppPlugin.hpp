#pragma once

#include <VoltMod/App/Plugin.hpp>
#include <optional>
#include <string_view>
#include <type_traits>

namespace VoltMod
{

/**
 * @brief Owns one @p TApp per load cycle. VOLTMOD_PLUGIN(TApp) uses it for you.
 *
 * @p TApp is built from `Runtime&` and has `bool Start()`. Optional members, called when present:
 * `void OnMapChanged()` at each map start, and
 * `bool OnPlayerChat(Player*, std::string_view message, bool teamChat)` in place of the default
 * chat handling, which consumes menu input and then dispatches chat commands.
 */
template <class TApp>
class AppPlugin final : public Plugin
{
protected:
    bool OnLoad(Runtime& runtime) override
    {
        _app.emplace(runtime);
        return _app->Start();
    }

    void OnUnload() override { _app.reset(); }

    void OnServerStartup(std::string_view /*mapName*/) override
    {
        if constexpr (requires(TApp& app) { app.OnMapChanged(); })
        {
            if (_app)
                _app->OnMapChanged();
        }
    }

    bool OnPlayerChat(Player* player, std::string_view message, bool teamChat) override
    {
        if constexpr (requires(TApp& app) { app.OnPlayerChat(player, message, teamChat); })
            return _app && _app->OnPlayerChat(player, message, teamChat);
        else
            return Plugin::OnPlayerChat(player, message, teamChat);
    }

private:
    std::optional<TApp> _app;
};

/** What VOLTMOD_PLUGIN(T) instantiates: @p T itself when it derives from Plugin, else AppPlugin<T>. */
template <class T>
using PluginFor = std::conditional_t<std::is_base_of_v<Plugin, T>, T, AppPlugin<T>>;

}  // namespace VoltMod
