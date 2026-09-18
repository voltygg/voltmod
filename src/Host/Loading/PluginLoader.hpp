#pragma once

#include "Host/Loading/InstalledPlugins.hpp"
#include "Host/Loading/SharedLibrary.hpp"
#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** One plugin whose library is open and whose Load returned true. */
struct LoadedPlugin
{
    PluginManifest Manifest;
    const PluginDescriptor* Descriptor = nullptr;
    SharedLibrary Code;  ///< last member: freed only after Unload has returned

    /** The plugin's own build stamp, falling back to the manifest version when it carries none. */
    std::string_view Version() const;
};

/**
 * @brief Finds the installed plugins and loads them in dependency order.
 *
 * A load or unload asked for from the console is deferred, never acted on where it was asked: the
 * request arrives inside the console-command dispatch, and freeing a library there would pull the
 * ground out from under the call in flight. @ref RunPending runs them at the start of the next frame.
 */
class PluginLoader
{
public:
    enum class ActionKind
    {
        Load,
        Unload,
        Reload
    };

    explicit PluginLoader(PluginHost& host);
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    /** Load every installed plugin the dependency order allows. */
    void Start();

    /** Unload every loaded plugin, newest first. */
    void Stop();

    /** Take @p kind on @p name at the start of the next frame. */
    void Defer(ActionKind kind, std::string_view name);

    /** Act on what was deferred. The frame hook calls this before the frame reaches plugins. */
    void RunPending();

    /** In load order; unload runs it backwards. */
    const std::vector<LoadedPlugin>& Loaded() const { return _loaded; }

    /** The loaded plugin @p name, or nullptr after logging that it is not loaded. */
    LoadedPlugin* RequireLoaded(std::string_view name);

private:
    struct PendingAction
    {
        ActionKind Kind = ActionKind::Load;
        std::string Name;
    };

    /** Open the library, check its descriptor and give it a view of the host. */
    Status LoadOne(const PluginManifest& manifest);
    /** Unload @p name, report whatever it left behind, then free its library. */
    void UnloadOne(std::string_view name);

    /** Plan @p installed, log what it refuses, and load in order whatever @p wanted accepts. */
    void LoadGroup(std::span<const PluginManifest> installed, const std::function<bool(std::string_view)>& wanted);

    LoadedPlugin* FindLoaded(std::string_view name);
    std::vector<PluginManifest> LoadedManifests() const;

    void RunLoad(std::string_view name);
    void RunUnload(std::string_view name);
    void RunReload(std::string_view name);

    PluginHost& _host;
    std::vector<LoadedPlugin> _loaded;
    std::vector<PendingAction> _pending;
};

}  // namespace VoltMod
