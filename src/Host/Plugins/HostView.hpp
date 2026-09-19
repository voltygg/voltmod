#pragma once

#include "Host/Plugins/CallbackList.hpp"
#include "Host/Plugins/CommandNames.hpp"
#include "Host/Plugins/ServiceTable.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Host/IHostEvents.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <VoltMod/Host/IHostServices.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The state one host owns once per process, which every plugin's view reaches through.
 *
 * @ref PluginHost owns this and outlives every view over it. Game thread only; nothing here locks.
 */
struct HostState
{
    SourceMM::ISmmAPI* Metamod = nullptr;
    KHook::IKHook* HookDispatcher = nullptr;
    IHostGameData* GameData = nullptr;

    uint64_t SchemaLayoutStamp = 0;  ///< zero until the host has checked its own layout
    bool SchemaVerified = false;

    uint64_t NextToken = 1;  ///< unique across every event and the service table, never zero, never reused
    uint64_t NextOrder = 1;  ///< load positions keep rising, so a reloaded plugin dispatches last

    CallbackList<IHostEvents::FrameFn> Frame;
    CallbackList<IHostEvents::ServerStartupFn> ServerStartup;
    CallbackList<IHostEvents::ClientConnectedFn> ClientConnected;
    CallbackList<IHostEvents::ClientDisconnectedFn> ClientDisconnected;
    CallbackList<IHostEvents::ClientFullyConnectedFn> ClientFullyConnected;
    CallbackList<IHostEvents::ClientSettingsChangedFn> ClientSettingsChanged;
    CallbackList<IHostEvents::ConsoleCommandFn> ConsoleCommand;
    CallbackList<IHostEvents::CheckTransmitFn> CheckTransmit;

    ServiceTable Services;
    CommandNames Commands;
};

/** What a plugin had not released when it was removed. The host drops each and warns about it. */
struct Unreleased
{
    std::vector<std::string_view> Subscriptions;  ///< the event each was taken on
    std::vector<std::string> Services;

    bool Any() const;
};

/**
 * @brief One loaded plugin's view of the host, and the record of what it took.
 *
 * A plugin reaches the host only through its own view, which is how the host knows whose
 * subscription, publication or command name every call is. Owned by @ref PluginHost and valid
 * from AddPlugin until RemovePlugin.
 */
class HostView final : public IHost, public IHostEvents, public IHostServices
{
public:
    HostView(HostState& state, std::string name, std::string logTag, std::string version, uint64_t order);

    HostView(const HostView&) = delete;
    HostView& operator=(const HostView&) = delete;

    std::string_view PluginName() const { return _name; }

    /** Silence this plugin below @p level. `volt log <name> <level>` is what calls it. */
    void SetMinLogLevel(LogLevel level) { _minLevel = level; }

    /** Remove what the plugin still holds. Command names are the host's to remove, so they are not reported. */
    Unreleased RemoveAll();

    std::string_view Name() const override;
    std::string_view Version() const override;
    SourceMM::ISmmAPI* Metamod() const override;
    KHook::IKHook* HookDispatcher() const override;
    IHostEvents& Events() override;
    IHostServices& Services() override;
    IHostGameData* GameData() const override;
    bool RegisterCommand(std::string_view name) override;
    bool IsCommandRegistered(std::string_view name) const override;
    void WriteLog(uint8_t level, std::string_view text) override;
    uint8_t MinLogLevel() const override;
    uint64_t SchemaLayoutStamp() const override;
    bool SchemaVerified() const override;

    uint64_t OnFrame(FrameFn callback, void* context) override;
    uint64_t OnServerStartup(ServerStartupFn callback, void* context) override;
    uint64_t OnClientConnected(ClientConnectedFn callback, void* context) override;
    uint64_t OnClientDisconnected(ClientDisconnectedFn callback, void* context) override;
    uint64_t OnClientFullyConnected(ClientFullyConnectedFn callback, void* context) override;
    uint64_t OnClientSettingsChanged(ClientSettingsChangedFn callback, void* context) override;
    uint64_t OnConsoleCommand(ConsoleCommandFn callback, void* context) override;
    uint64_t OnCheckTransmit(CheckTransmitFn callback, void* context) override;

    void Publish(std::string_view name, void* implementation) override;
    void Unpublish(std::string_view name) override;
    void* Find(std::string_view name) override;
    uint64_t OnChanged(ChangedFn callback, void* context) override;

    /** Overrides both interfaces' Unsubscribe: there is one token space, and a token this plugin
     *  never took is ignored. */
    void Unsubscribe(uint64_t token) override;

private:
    struct Subscribed
    {
        uint64_t Token = 0;
        std::string_view Event;        ///< names it in a leak line
        std::function<void()> Remove;  ///< drops the token from the list it went into
    };

    template <class Fn>
    uint64_t Subscribe(std::string_view event, CallbackList<Fn>& list, Fn callback, void* context);

    HostState& _state;
    std::string _name;
    std::string _logTag;  ///< what this plugin's log lines are prefixed with
    std::string _version;
    LogLevel _minLevel = LogLevel::Info;
    uint64_t _order = 0;
    std::vector<Subscribed> _subscriptions;  ///< in the order the plugin took them
};

}  // namespace VoltMod
