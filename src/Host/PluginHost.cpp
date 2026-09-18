#include "Host/PluginHost.hpp"

#include <VoltMod/Core/Log.hpp>
#include <format>
#include <VoltMod/Host/Abi.hpp>
#include <algorithm>
#include <utility>

namespace VoltMod
{

bool PluginLeaks::Any() const
{
    return !Subscriptions.empty() || !Services.empty() || !Commands.empty();
}

HostString Borrowed(std::string_view text)
{
    return {.Data = text.data(), .Length = text.size()};
}

std::string_view Text(HostString text)
{
    return text.Data != nullptr ? std::string_view(text.Data, text.Length) : std::string_view();
}

PluginContext::PluginContext(PluginHost& host, std::string name, uint64_t order)
    : _host(host), _name(std::move(name)), _home("addons/" + _name), _order(order)
{}

uint32_t PluginContext::AbiVersion() const
{
    return HostAbiVersion;
}

HostString PluginContext::Name() const
{
    return Borrowed(_name);
}

HostString PluginContext::HomeDirectory() const
{
    return Borrowed(_home);
}

SourceMM::ISmmAPI* PluginContext::Metamod() const
{
    return _host.Metamod();
}

KHook::IKHook* PluginContext::Detours() const
{
    return _host.Detours();
}

void* PluginContext::GetInterface(HostString name) const
{
    // The interfaces handed out are mutable; const here only says the lookup changes nothing.
    PluginContext* self = const_cast<PluginContext*>(this);
    const std::string_view wanted = Text(name);

    if (wanted == IHost::InterfaceName)
        return static_cast<IHost*>(self);
    if (wanted == IHostEvents::InterfaceName)
        return static_cast<IHostEvents*>(self);
    if (wanted == IHostServices::InterfaceName)
        return static_cast<IHostServices*>(self);
    if (wanted == IHostLog::InterfaceName)
        return static_cast<IHostLog*>(self);
    if (wanted == IHostSchema::InterfaceName)
        return static_cast<IHostSchema*>(self);
    // One resolution for the whole process, so this one is the host's, not the context's.
    if (wanted == IHostGameData::InterfaceName)
        return _host.GameData();
    return nullptr;
}

bool PluginContext::ClaimCommand(HostString name)
{
    return _host.ClaimCommand(*this, Text(name));
}

uint64_t PluginContext::LayoutStamp() const
{
    return _host.SchemaLayoutStamp();
}

bool PluginContext::Verified() const
{
    return _host.SchemaVerified();
}

void PluginContext::SetTag(HostString tag)
{
    const std::string_view wanted = Text(tag);
    _tag = wanted.empty() ? _name : std::string(wanted);
}

void PluginContext::Write(uint8_t level, HostString text)
{
    const auto wanted = static_cast<LogLevel>(level);
    if (wanted < _minLevel)
        return;

    // Every plugin prints through the host's own handler, so a server reads one stream.
    Log::Emit(wanted, std::format("[{}] {}", _tag.empty() ? _name : _tag, Text(text)));
}

uint8_t PluginContext::MinLevel()
{
    return static_cast<uint8_t>(_minLevel);
}

template <class Fn>
HostToken PluginContext::Take(HostEvent event, Subscribers<Fn>& subscribers, Fn call, void* context)
{
    if (call == nullptr)
        return 0;

    const HostToken token = _host.NextToken();
    subscribers.Add(token, _order, call, context);
    _subscriptions.push_back({.Token = token, .Event = event});
    return token;
}

HostToken PluginContext::SubscribeFrame(FrameFn call, void* context)
{
    return Take(HostEvent::Frame, _host._frame, call, context);
}

HostToken PluginContext::SubscribeServerStartup(ServerStartupFn call, void* context)
{
    return Take(HostEvent::ServerStartup, _host._serverStartup, call, context);
}

HostToken PluginContext::SubscribeClientConnected(ClientConnectedFn call, void* context)
{
    return Take(HostEvent::ClientConnected, _host._clientConnected, call, context);
}

HostToken PluginContext::SubscribeClientDisconnected(ClientDisconnectedFn call, void* context)
{
    return Take(HostEvent::ClientDisconnected, _host._clientDisconnected, call, context);
}

HostToken PluginContext::SubscribeClientFullyConnected(ClientFullyConnectedFn call, void* context)
{
    return Take(HostEvent::ClientFullyConnected, _host._clientFullyConnected, call, context);
}

HostToken PluginContext::SubscribeClientSettingsChanged(ClientSettingsChangedFn call, void* context)
{
    return Take(HostEvent::ClientSettingsChanged, _host._clientSettingsChanged, call, context);
}

HostToken PluginContext::SubscribeConsoleCommand(ConsoleCommandFn call, void* context)
{
    return Take(HostEvent::ConsoleCommand, _host._consoleCommand, call, context);
}

HostToken PluginContext::SubscribeCheckTransmit(CheckTransmitFn call, void* context)
{
    return Take(HostEvent::CheckTransmit, _host._checkTransmit, call, context);
}

void PluginContext::Publish(HostString name, void* implementation)
{
    _host.Publish(*this, Text(name), implementation);
}

void PluginContext::Unpublish(HostString name)
{
    _host.Unpublish(*this, Text(name));
}

void* PluginContext::Find(HostString name)
{
    return _host.FindService(Text(name));
}

HostToken PluginContext::SubscribeChanged(ChangedFn call, void* context)
{
    const HostToken token = Take(HostEvent::ServicesChanged, _host._servicesChanged, call, context);
    if (token != 0)
        _host.ReplayServices(call, context);
    return token;
}

void PluginContext::Unsubscribe(HostToken token)
{
    const auto held = std::ranges::find(_subscriptions, token, &Subscribed::Token);
    if (held == _subscriptions.end())
        return;

    _host.RemoveFrom(held->Event, token);
    _subscriptions.erase(held);
}

std::vector<HostEvent> PluginContext::DropSubscriptions()
{
    std::vector<HostEvent> leaked;
    leaked.reserve(_subscriptions.size());
    for (const Subscribed& held : _subscriptions)
    {
        _host.RemoveFrom(held.Event, held.Token);
        leaked.push_back(held.Event);
    }
    _subscriptions.clear();
    return leaked;
}

PluginHost::PluginHost(SourceMM::ISmmAPI* metamod, KHook::IKHook* detours, IHostGameData* gameData)
    : _metamod(metamod), _detours(detours), _gameData(gameData)
{}

PluginHost::~PluginHost() = default;

void PluginHost::SetSchemaLayout(uint64_t stamp, bool verified)
{
    _schemaLayoutStamp = stamp;
    _schemaVerified = verified;
}

PluginContext* PluginHost::OpenPlugin(std::string_view name)
{
    if (name.empty() || FindPlugin(name) != nullptr)
        return nullptr;

    _plugins.push_back(std::make_unique<PluginContext>(*this, std::string(name), _nextOrder++));
    return _plugins.back().get();
}

PluginContext* PluginHost::FindPlugin(std::string_view name)
{
    const auto found = std::ranges::find_if(
        _plugins, [&](const std::unique_ptr<PluginContext>& plugin) { return plugin->PluginName() == name; });
    return found != _plugins.end() ? found->get() : nullptr;
}

PluginContext* PluginHost::ContextFor(std::string_view name)
{
    const auto found = std::ranges::find_if(
        _plugins, [name](const auto& plugin) { return plugin->PluginName() == name; });
    return found == _plugins.end() ? nullptr : found->get();
}

PluginLeaks PluginHost::ClosePlugin(std::string_view name)
{
    PluginLeaks leaks;
    const auto found = std::ranges::find_if(
        _plugins, [&](const std::unique_ptr<PluginContext>& plugin) { return plugin->PluginName() == name; });
    if (found == _plugins.end())
        return leaks;

    PluginContext& plugin = **found;
    // Before the withdrawals, so the plugin on its way out is not told about its own.
    leaks.Subscriptions = plugin.DropSubscriptions();

    for (const Service& service : _services)
        if (service.Owner == &plugin)
            leaks.Services.push_back(service.Name);
    std::erase_if(_services, [&](const Service& service) { return service.Owner == &plugin; });

    for (const Claim& claim : _commands)
        if (claim.Owner == &plugin)
            leaks.Commands.push_back(claim.Name);
    std::erase_if(_commands, [&](const Claim& claim) { return claim.Owner == &plugin; });

    // Raise once the table is settled: a Changed callback may publish or withdraw in turn.
    for (const std::string& withdrawn : leaks.Services)
        RaiseServicesChanged(withdrawn, false);

    _plugins.erase(found);
    return leaks;
}

void PluginHost::RemoveFrom(HostEvent event, HostToken token)
{
    switch (event)
    {
    case HostEvent::Frame:
        _frame.Remove(token);
        break;
    case HostEvent::ServerStartup:
        _serverStartup.Remove(token);
        break;
    case HostEvent::ClientConnected:
        _clientConnected.Remove(token);
        break;
    case HostEvent::ClientDisconnected:
        _clientDisconnected.Remove(token);
        break;
    case HostEvent::ClientFullyConnected:
        _clientFullyConnected.Remove(token);
        break;
    case HostEvent::ClientSettingsChanged:
        _clientSettingsChanged.Remove(token);
        break;
    case HostEvent::ConsoleCommand:
        _consoleCommand.Remove(token);
        break;
    case HostEvent::CheckTransmit:
        _checkTransmit.Remove(token);
        break;
    case HostEvent::ServicesChanged:
        _servicesChanged.Remove(token);
        break;
    }
}

void PluginHost::RaiseFrame()
{
    _frame.Dispatch([](IHostEvents::FrameFn call, void* context) {
        call(context);
        return false;
    });
}

void PluginHost::RaiseServerStartup(std::string_view mapName)
{
    const HostString map = Borrowed(mapName);
    _serverStartup.Dispatch([&](IHostEvents::ServerStartupFn call, void* context) {
        call(context, map);
        return false;
    });
}

void PluginHost::RaiseClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address)
{
    const HostString clientName = Borrowed(name);
    const HostString clientAddress = Borrowed(address);
    _clientConnected.Dispatch([&](IHostEvents::ClientConnectedFn call, void* context) {
        call(context, slot, steamId, clientName, clientAddress);
        return false;
    });
}

void PluginHost::RaiseClientDisconnected(int slot)
{
    _clientDisconnected.Dispatch([&](IHostEvents::ClientDisconnectedFn call, void* context) {
        call(context, slot);
        return false;
    });
}

void PluginHost::RaiseClientFullyConnected(int slot)
{
    _clientFullyConnected.Dispatch([&](IHostEvents::ClientFullyConnectedFn call, void* context) {
        call(context, slot);
        return false;
    });
}

void PluginHost::RaiseClientSettingsChanged(int slot)
{
    _clientSettingsChanged.Dispatch([&](IHostEvents::ClientSettingsChangedFn call, void* context) {
        call(context, slot);
        return false;
    });
}

bool PluginHost::RaiseConsoleCommand(std::string_view name, std::string_view arguments, int slot)
{
    const HostString commandName = Borrowed(name);
    const HostString commandArguments = Borrowed(arguments);
    return _consoleCommand.Dispatch([&](IHostEvents::ConsoleCommandFn call, void* context) {
        return call(context, commandName, commandArguments, slot);
    });
}

void PluginHost::RaiseCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    _checkTransmit.Dispatch([&](IHostEvents::CheckTransmitFn call, void* context) {
        call(context, infoList, infoCount);
        return false;
    });
}

bool PluginHost::ClaimCommand(const PluginContext& plugin, std::string_view name)
{
    if (name.empty())
        return false;

    const auto held = std::ranges::find(_commands, name, &Claim::Name);
    if (held != _commands.end())
    {
        if (held->Owner == &plugin)  // re-claiming its own name is not a conflict
            return true;

        const std::string_view holder = held->Owner ? held->Owner->PluginName() : "the host";
        Log::Error("Command '{}' is already registered by {}, so {} cannot have it.", name, holder,
                   plugin.PluginName());
        return false;
    }

    _commands.push_back({.Name = std::string(name), .Owner = &plugin});
    return true;
}

std::string_view PluginHost::CommandHolder(std::string_view name) const
{
    const auto held = std::ranges::find(_commands, name, &Claim::Name);
    if (held == _commands.end())
        return {};
    return held->Owner ? held->Owner->PluginName() : std::string_view("the host");
}

void PluginHost::ReserveCommand(std::string_view name)
{
    if (!name.empty() && std::ranges::find(_commands, name, &Claim::Name) == _commands.end())
        _commands.push_back({.Name = std::string(name), .Owner = nullptr});
}

void PluginHost::Publish(const PluginContext& plugin, std::string_view name, void* implementation)
{
    if (name.empty() || implementation == nullptr)
        return;

    const auto held = std::ranges::find(_services, name, &Service::Name);
    if (held == _services.end())
    {
        _services.push_back({.Name = std::string(name), .Implementation = implementation, .Owner = &plugin});
    }
    else if (held->Owner == &plugin)
    {
        held->Implementation = implementation;  // its own name to refresh
    }
    else
    {
        return;  // a peer's live pointer is never swapped out from under it
    }

    RaiseServicesChanged(name, true);
}

void PluginHost::Unpublish(const PluginContext& plugin, std::string_view name)
{
    const auto held = std::ranges::find(_services, name, &Service::Name);
    if (held == _services.end() || held->Owner != &plugin)
        return;

    const std::string withdrawn = std::move(held->Name);
    _services.erase(held);
    RaiseServicesChanged(withdrawn, false);
}

void* PluginHost::FindService(std::string_view name) const
{
    const auto held = std::ranges::find(_services, name, &Service::Name);
    return held != _services.end() ? held->Implementation : nullptr;
}

std::string_view PluginHost::ServiceOwner(std::string_view name) const
{
    const auto held = std::ranges::find(_services, name, &Service::Name);
    return held != _services.end() ? held->Owner->PluginName() : std::string_view();
}

void PluginHost::ReplayServices(IHostServices::ChangedFn call, void* context) const
{
    // Copy the names out first: a replayed callback may publish or withdraw as it goes.
    std::vector<std::string> published;
    published.reserve(_services.size());
    for (const Service& service : _services)
        published.push_back(service.Name);

    for (const std::string& name : published)
        call(context, Borrowed(name), true);
}

void PluginHost::RaiseServicesChanged(std::string_view name, bool published)
{
    const HostString changed = Borrowed(name);
    _servicesChanged.Dispatch([&](IHostServices::ChangedFn call, void* context) {
        call(context, changed, published);
        return false;
    });
}

}  // namespace VoltMod
