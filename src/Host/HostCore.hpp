#pragma once

#include "Host/EventFanOut.hpp"

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/HostTypes.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Host/IHostEvents.hpp>
#include <VoltMod/Host/IHostServices.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

class HostCore;

/** Which fan-out a subscription was taken on. Names the event in a leak line. */
enum class HostEvent
{
    Frame,
    ServerStartup,
    ClientConnected,
    ClientDisconnected,
    ClientFullyConnected,
    ClientSettingsChanged,
    ConsoleCommand,
    CheckTransmit,
    ServicesChanged,
};

/** What a plugin still held when its context closed. The host drops each and logs it as a leak. */
struct PluginLeaks
{
    std::vector<HostEvent> Subscriptions;
    std::vector<std::string> Services;
    std::vector<std::string> Commands;

    bool Any() const;
};

/** Borrow @p text for the duration of one host call. */
HostString Borrowed(std::string_view text);

/** What @p text borrows, empty when it carries nothing. */
std::string_view Text(HostString text);

/**
 * @brief One loaded plugin's view of the host, and the record of what it took.
 *
 * A plugin reaches the host only through its own context, which is how the host knows whose
 * subscription, publication or command claim every call is. Owned by @ref HostCore and valid from
 * OpenPlugin until ClosePlugin.
 */
class PluginContext final : public IHost, public IHostEvents, public IHostServices
{
public:
    PluginContext(HostCore& host, std::string name, uint64_t order);

    PluginContext(const PluginContext&) = delete;
    PluginContext& operator=(const PluginContext&) = delete;

    std::string_view PluginName() const { return _name; }
    /** The plugin's load position: the first key of every fan-out's dispatch order. */
    uint64_t Order() const { return _order; }

    uint32_t AbiVersion() const override;
    HostString Name() const override;
    HostString HomeDirectory() const override;
    SourceMM::ISmmAPI* Metamod() const override;
    KHook::IKHook* Detours() const override;
    void* GetInterface(HostString name) const override;
    bool ClaimCommand(HostString name) override;

    HostToken SubscribeFrame(FrameFn call, void* context) override;
    HostToken SubscribeServerStartup(ServerStartupFn call, void* context) override;
    HostToken SubscribeClientConnected(ClientConnectedFn call, void* context) override;
    HostToken SubscribeClientDisconnected(ClientDisconnectedFn call, void* context) override;
    HostToken SubscribeClientFullyConnected(ClientFullyConnectedFn call, void* context) override;
    HostToken SubscribeClientSettingsChanged(ClientSettingsChangedFn call, void* context) override;
    HostToken SubscribeConsoleCommand(ConsoleCommandFn call, void* context) override;
    HostToken SubscribeCheckTransmit(CheckTransmitFn call, void* context) override;

    void Publish(HostString name, void* implementation) override;
    void Unpublish(HostString name) override;
    void* Find(HostString name) override;
    HostToken SubscribeChanged(ChangedFn call, void* context) override;

    /** Overrides both interfaces' Unsubscribe: there is one token space, and a token this plugin
     *  never took is ignored. */
    void Unsubscribe(HostToken token) override;

private:
    // One unit split in two: the context is the plugin-facing side of the host's own state.
    friend class HostCore;

    struct Subscribed
    {
        HostToken Token = 0;
        HostEvent Event = HostEvent::Frame;
    };

    template <class Fn>
    HostToken Take(HostEvent event, EventFanOut<Fn>& fanOut, Fn call, void* context);

    /** Remove every subscription still held and report which event each was on. */
    std::vector<HostEvent> DropSubscriptions();

    HostCore& _host;
    std::string _name;
    std::string _home;
    uint64_t _order = 0;
    std::vector<Subscribed> _subscriptions;  ///< in the order the plugin took them
};

/**
 * @brief The state one host owns once per process: the fan-outs, the service table, the command
 * claims and one context per loaded plugin.
 *
 * Game thread only, like everything a plugin reaches; nothing here locks. Free of the SDK so it is
 * unit-tested without the engine - @p metamod and @p detours are values it hands on unchanged.
 */
class HostCore
{
public:
    explicit HostCore(SourceMM::ISmmAPI* metamod = nullptr, KHook::IKHook* detours = nullptr);
    ~HostCore();

    HostCore(const HostCore&) = delete;
    HostCore& operator=(const HostCore&) = delete;

    /** Give @p name its own context, at the end of the dispatch order. Nullptr when it is open. */
    PluginContext* OpenPlugin(std::string_view name);

    /** Drop every subscription, publication and claim @p name still holds, and report them. */
    PluginLeaks ClosePlugin(std::string_view name);

    PluginContext* FindPlugin(std::string_view name);

    SourceMM::ISmmAPI* Metamod() const { return _metamod; }
    KHook::IKHook* Detours() const { return _detours; }

    void RaiseFrame();
    void RaiseServerStartup(std::string_view mapName);
    void RaiseClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address);
    void RaiseClientDisconnected(int slot);
    void RaiseClientFullyConnected(int slot);
    void RaiseClientSettingsChanged(int slot);
    /** True when a plugin answered it: later plugins never see it and the engine call is blocked. */
    bool RaiseConsoleCommand(std::string_view name, std::string_view arguments, int slot);
    void RaiseCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    /** The plugin holding @p name for its console commands, empty while the name is free. */
    std::string_view CommandHolder(std::string_view name) const;

    /** The plugin that published @p name, empty while nothing has. */
    std::string_view ServiceOwner(std::string_view name) const;

private:
    friend class PluginContext;

    struct Service
    {
        std::string Name;
        void* Implementation = nullptr;
        const PluginContext* Owner = nullptr;
    };

    struct Claim
    {
        std::string Name;
        const PluginContext* Owner = nullptr;
    };

    HostToken NextToken() { return _nextToken++; }

    void RemoveFrom(HostEvent event, HostToken token);

    bool ClaimCommand(const PluginContext& plugin, std::string_view name);
    void Publish(const PluginContext& plugin, std::string_view name, void* implementation);
    void Unpublish(const PluginContext& plugin, std::string_view name);
    void* FindService(std::string_view name) const;
    /** Hand @p call what is already in the table: what a late subscriber is promised. */
    void ReplayServices(IHostServices::ChangedFn call, void* context) const;
    void RaiseServicesChanged(std::string_view name, bool published);

    SourceMM::ISmmAPI* _metamod = nullptr;
    KHook::IKHook* _detours = nullptr;

    HostToken _nextToken = 1;  ///< unique across every fan-out, never zero, never reused
    uint64_t _nextOrder = 1;   ///< load positions keep rising, so a reloaded plugin dispatches last

    std::vector<std::unique_ptr<PluginContext>> _plugins;  ///< in load order

    EventFanOut<IHostEvents::FrameFn> _frame;
    EventFanOut<IHostEvents::ServerStartupFn> _serverStartup;
    EventFanOut<IHostEvents::ClientConnectedFn> _clientConnected;
    EventFanOut<IHostEvents::ClientDisconnectedFn> _clientDisconnected;
    EventFanOut<IHostEvents::ClientFullyConnectedFn> _clientFullyConnected;
    EventFanOut<IHostEvents::ClientSettingsChangedFn> _clientSettingsChanged;
    EventFanOut<IHostEvents::ConsoleCommandFn> _consoleCommand;
    EventFanOut<IHostEvents::CheckTransmitFn> _checkTransmit;
    EventFanOut<IHostServices::ChangedFn> _servicesChanged;

    std::vector<Service> _services;  ///< in publish order, which is the replay order
    std::vector<Claim> _commands;
};

}  // namespace VoltMod
