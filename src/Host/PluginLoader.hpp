#pragma once

#include "Host/Library.hpp"
#include "Host/PluginHost.hpp"
#include "Host/PluginOrder.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Finds the installed plugins, loads them in dependency order, and serves `volt`.
 *
 * A load or unload asked for from the console is queued, never acted on where it was asked: the
 * request arrives inside the console-command dispatch, and freeing a library there would pull the
 * ground out from under the call in flight. @ref RunQueuedRequests runs them at the start of the
 * next frame.
 */
class PluginLoader
{
public:
    explicit PluginLoader(PluginHost& host);
    ~PluginLoader();

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    /** Register `volt`, then load every installed plugin the dependency order allows. */
    void Start();

    /** Unload every loaded plugin, newest first, and drop `volt`. */
    void Stop();

    /** Act on what `volt` queued. The frame hook calls this before the frame reaches plugins. */
    void RunQueuedRequests();

private:
    /** One `addons/<name>/plugin.json` that parsed. */
    struct InstalledPlugin
    {
        PluginManifest Manifest;
        std::string Version;
    };

    /** One plugin whose library is open and whose Load returned true. */
    struct LoadedPlugin
    {
        PluginManifest Manifest;
        std::string Version;
        const PluginDescriptor* Descriptor = nullptr;
        Library Code;  ///< last member: freed only after Unload has returned
    };

    enum class RequestKind
    {
        Load,
        Unload,
        Reload
    };

    struct Request
    {
        RequestKind Kind = RequestKind::Load;
        std::string Name;
    };

    /** Every plugin.json under an `addons` subdirectory that parsed; each one that did not is
     *  logged and left out. */
    static std::vector<InstalledPlugin> Discover();
    static std::filesystem::path LibraryPath(std::string_view name);

    /** Open the library, check its ABI version and give it a context. */
    Status LoadOne(const InstalledPlugin& plugin);
    /** Unload @p name, report whatever it left behind, then free its library. */
    void UnloadOne(std::string_view name);

    LoadedPlugin* FindLoaded(std::string_view name);
    std::vector<PluginManifest> LoadedManifests() const;

    void RunCommand(const CCommand& arguments);
    void Queue(RequestKind kind, std::string_view name);
    void PrintLoaded() const;
    void PrintStatus(std::string_view name);

    void RunLoad(std::string_view name);
    void RunUnload(std::string_view name);
    void RunReload(std::string_view name);

    PluginHost& _host;
    std::vector<LoadedPlugin> _loaded;  ///< in load order; unload runs it backwards
    std::vector<Request> _queue;
    std::unique_ptr<ServerCommand> _command;
};

}  // namespace VoltMod
