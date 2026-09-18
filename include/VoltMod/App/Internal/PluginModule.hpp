#pragma once

#include <VoltMod/App/Plugin.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod::Internal
{

/** The build stamp VOLTMOD_PLUGIN takes from the generated BuildInfo.hpp. */
struct PluginBuild
{
    const char* Version = "";
    const char* Commit = "";
    const char* Date = "";
};

template <class T>
concept PluginType = std::derived_from<T, Plugin> && std::constructible_from<T, Runtime&>;

using PluginFactory = std::unique_ptr<Plugin> (*)(Runtime&);

/** Owns the host connection, runtime, and one user plugin for each load cycle. */
class PluginModule
{
public:
    explicit PluginModule(PluginFactory factory) : _factory(factory) {}
    ~PluginModule();

    PluginModule(const PluginModule&) = delete;
    PluginModule& operator=(const PluginModule&) = delete;

    bool Attach(IHost& host, const PluginBuild& build, char* error, size_t errorSize) noexcept;
    void Detach() noexcept;
    const char* StatusJson() noexcept;

private:
    bool AttachImpl(IHost& host, const PluginBuild& build, char* error, size_t errorSize);
    void WriteFailure(char* error, size_t errorSize, std::string_view failure) noexcept;

    void HandleServerStartup(std::string_view mapName);
    bool HandleConsoleCommand(std::string_view name, std::string_view arguments, int slot);
    void SubscribeHostEvents();
    void Shutdown() noexcept;

    void HostFrame();
    void HostClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address);
    void HostClientDisconnected(int slot);
    void HostClientFullyConnected(int slot);
    void HostClientSettingsChanged(int slot);
    void HostCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    PluginFactory _factory;
    IHost* _host = nullptr;
    std::unique_ptr<Runtime> _runtime;
    Subscriptions _hostEvents;
    std::unique_ptr<Plugin> _plugin;
    std::string _status;
};

template <PluginType T>
PluginModule MakePluginModule()
{
    return PluginModule([](Runtime& runtime) -> std::unique_ptr<Plugin> { return std::make_unique<T>(runtime); });
}

}  // namespace VoltMod::Internal
