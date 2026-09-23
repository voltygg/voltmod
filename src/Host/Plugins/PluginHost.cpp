#include "Host/Plugins/PluginHost.hpp"

#include <algorithm>
#include <string>

namespace VoltMod
{

PluginHost::PluginHost(SourceMM::ISmmAPI* metamod, KHook::IKHook* hookDispatcher, IHostGameData* gameData)
    : _state{.Metamod = metamod, .HookDispatcher = hookDispatcher, .GameData = gameData}
{}

PluginHost::~PluginHost() = default;

void PluginHost::SetSchemaLayout(uint64_t stamp, bool verified)
{
    _state.SchemaLayoutStamp = stamp;
    _state.SchemaVerified = verified;
}

HostView* PluginHost::AddPlugin(std::string_view name, std::string_view logTag, std::string_view version)
{
    if (name.empty() || FindPlugin(name) != nullptr)
    {
        return nullptr;
    }

    _plugins.push_back(std::make_unique<HostView>(_state, std::string(name),
                                                  std::string(logTag.empty() ? name : logTag), std::string(version),
                                                  _state.NextOrder++));
    return _plugins.back().get();
}

HostView* PluginHost::FindPlugin(std::string_view name)
{
    const auto found =
        std::ranges::find_if(_plugins, [name](const auto& plugin) { return plugin->PluginName() == name; });
    return found != _plugins.end() ? found->get() : nullptr;
}

Unreleased PluginHost::RemovePlugin(std::string_view name)
{
    const auto found =
        std::ranges::find_if(_plugins, [name](const auto& plugin) { return plugin->PluginName() == name; });
    if (found == _plugins.end())
    {
        return {};
    }

    Unreleased unreleased = (*found)->RemoveAll();
    _plugins.erase(found);
    return unreleased;
}

void PluginHost::RaiseFrame()
{
    _state.Frame.Dispatch([](IHostEvents::FrameFn callback, void* context) {
        callback(context);
        return false;
    });
}

void PluginHost::RaiseServerStartup(std::string_view mapName)
{
    _state.ServerStartup.Dispatch([&](IHostEvents::ServerStartupFn callback, void* context) {
        callback(context, mapName);
        return false;
    });
}

void PluginHost::RaiseClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address)
{
    // First, so a pick a plugin restores on connect survives.
    _state.Languages.Reset(slot);
    _state.ClientConnected.Dispatch([&](IHostEvents::ClientConnectedFn callback, void* context) {
        callback(context, slot, steamId, name, address);
        return false;
    });
}

void PluginHost::RaiseClientDisconnected(int slot)
{
    _state.ClientDisconnected.Dispatch([&](IHostEvents::ClientDisconnectedFn callback, void* context) {
        callback(context, slot);
        return false;
    });
    _state.Languages.Reset(slot);
}

void PluginHost::RaiseClientFullyConnected(int slot)
{
    _state.ClientFullyConnected.Dispatch([&](IHostEvents::ClientFullyConnectedFn callback, void* context) {
        callback(context, slot);
        return false;
    });
}

void PluginHost::RaiseClientSettingsChanged(int slot)
{
    _state.ClientSettingsChanged.Dispatch([&](IHostEvents::ClientSettingsChangedFn callback, void* context) {
        callback(context, slot);
        return false;
    });
}

bool PluginHost::RaiseConsoleCommand(std::string_view name, std::string_view arguments, int slot)
{
    return _state.ConsoleCommand.Dispatch([&](IHostEvents::ConsoleCommandFn callback, void* context) {
        return callback(context, name, arguments, slot);
    });
}

void PluginHost::RaiseCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    _state.CheckTransmit.Dispatch([&](IHostEvents::CheckTransmitFn callback, void* context) {
        callback(context, infoList, infoCount);
        return false;
    });
}

void PluginHost::RaiseBuildGameSessionManifest(IEntityResourceManifest* manifest)
{
    _state.BuildGameSessionManifest.Dispatch([&](IHostEvents::BuildGameSessionManifestFn callback, void* context) {
        callback(context, manifest);
        return false;
    });
}

std::string_view PluginHost::CommandOwner(std::string_view name) const
{
    return _state.Commands.OwnerOf(name);
}

void PluginHost::RegisterHostCommand(std::string_view name)
{
    _state.Commands.RegisterForHost(name);
}

}  // namespace VoltMod
