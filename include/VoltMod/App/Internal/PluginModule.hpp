#pragma once

#include <VoltMod/App/Plugin.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Unsafe/UnsafeServices.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod::Internal
{

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

    bool Attach(IHost& host, char* error, size_t errorSize) noexcept;
    void Detach() noexcept;
    const char* StatusJson() noexcept;

private:
    bool AttachImpl(IHost& host, char* error, size_t errorSize);
    /** Log the load steps, hand the host @p reason, and tear down what was built. */
    bool Refuse(std::string_view reason, char* error, size_t errorSize);

    void OnServerStartup(std::string_view mapName);
    bool OnConsoleCommand(std::string_view name, std::string_view arguments, int slot);
    void SubscribeHostEvents();
    void Shutdown() noexcept;

    void OnFrame();
    bool OnClientConnecting(int slot, int64_t steamId, std::string_view name, char* reason, size_t reasonSize);
    void OnClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address);
    void OnClientDisconnected(int slot);
    void OnClientFullyConnected(int slot);
    void OnClientSettingsChanged(int slot);
    void OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);
    void OnBuildGameSessionManifest(IEntityResourceManifest* manifest);

    PluginFactory _factory;
    IHost* _host = nullptr;
    /** Declared above the runtime, whose services keep references into it. */
    std::unique_ptr<UnsafeServices> _unsafe;
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
