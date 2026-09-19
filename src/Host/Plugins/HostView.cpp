#include "Host/Plugins/HostView.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace VoltMod
{

bool Unreleased::Any() const
{
    return !Subscriptions.empty() || !Services.empty();
}

HostView::HostView(HostState& state, std::string name, std::string logTag, std::string version, uint64_t order)
    : _state(state), _name(std::move(name)), _logTag(std::move(logTag)), _version(std::move(version)), _order(order)
{}

std::string_view HostView::Name() const
{
    return _name;
}

std::string_view HostView::Version() const
{
    return _version;
}

SourceMM::ISmmAPI* HostView::Metamod() const
{
    return _state.Metamod;
}

KHook::IKHook* HostView::HookDispatcher() const
{
    return _state.HookDispatcher;
}

IHostEvents& HostView::Events()
{
    return *this;
}

IHostServices& HostView::Services()
{
    return *this;
}

IHostGameData* HostView::GameData() const
{
    return _state.GameData;
}

bool HostView::RegisterCommand(std::string_view name)
{
    if (_state.Commands.Register(this, _name, name))
        return true;

    Log::Error("Command '{}' is already registered by {}, so {} cannot have it.", name, _state.Commands.OwnerOf(name),
               _name);
    return false;
}

bool HostView::IsCommandRegistered(std::string_view name) const
{
    return !_state.Commands.OwnerOf(name).empty();
}

std::string_view HostView::PlayerLanguage(int slot) const
{
    return IsValidSlot(slot) ? std::string_view(_state.Languages[slot]) : std::string_view{};
}

void HostView::SetPlayerLanguage(int slot, std::string_view lang)
{
    if (IsValidSlot(slot))
        _state.Languages[slot] = lang;
}

void HostView::WriteLog(uint8_t level, std::string_view text)
{
    const auto wanted = static_cast<LogLevel>(level);
    if (wanted < _minLevel)
        return;

    // Every plugin prints through the host's own handler, so a server reads one stream.
    Log::Emit(wanted, std::format("[{}] {}", _logTag, text));
}

uint8_t HostView::MinLogLevel() const
{
    return static_cast<uint8_t>(_minLevel);
}

uint64_t HostView::SchemaLayoutStamp() const
{
    return _state.SchemaLayoutStamp;
}

bool HostView::SchemaVerified() const
{
    return _state.SchemaVerified;
}

template <class Fn>
uint64_t HostView::Subscribe(std::string_view event, CallbackList<Fn>& list, Fn callback, void* context)
{
    if (callback == nullptr)
        return 0;

    const uint64_t token = _state.NextToken++;
    list.Add(token, _order, callback, context);
    _subscriptions.push_back({.Token = token, .Event = event, .Remove = [&list, token] { list.Remove(token); }});
    return token;
}

uint64_t HostView::OnFrame(FrameFn callback, void* context)
{
    return Subscribe("frame", _state.Frame, callback, context);
}

uint64_t HostView::OnServerStartup(ServerStartupFn callback, void* context)
{
    return Subscribe("server startup", _state.ServerStartup, callback, context);
}

uint64_t HostView::OnClientConnected(ClientConnectedFn callback, void* context)
{
    return Subscribe("client connected", _state.ClientConnected, callback, context);
}

uint64_t HostView::OnClientDisconnected(ClientDisconnectedFn callback, void* context)
{
    return Subscribe("client disconnected", _state.ClientDisconnected, callback, context);
}

uint64_t HostView::OnClientFullyConnected(ClientFullyConnectedFn callback, void* context)
{
    return Subscribe("client fully connected", _state.ClientFullyConnected, callback, context);
}

uint64_t HostView::OnClientSettingsChanged(ClientSettingsChangedFn callback, void* context)
{
    return Subscribe("client settings changed", _state.ClientSettingsChanged, callback, context);
}

uint64_t HostView::OnConsoleCommand(ConsoleCommandFn callback, void* context)
{
    return Subscribe("console command", _state.ConsoleCommand, callback, context);
}

uint64_t HostView::OnCheckTransmit(CheckTransmitFn callback, void* context)
{
    return Subscribe("check transmit", _state.CheckTransmit, callback, context);
}

void HostView::Publish(std::string_view name, void* implementation)
{
    _state.Services.Publish(this, name, implementation);
}

void HostView::Unpublish(std::string_view name)
{
    _state.Services.Unpublish(this, name);
}

void* HostView::Find(std::string_view name)
{
    return _state.Services.Find(name);
}

uint64_t HostView::OnChanged(ChangedFn callback, void* context)
{
    const uint64_t token = Subscribe("services changed", _state.Services.Changed(), callback, context);
    if (token != 0)
        _state.Services.NotifyPublished(callback, context);
    return token;
}

void HostView::Unsubscribe(uint64_t token)
{
    const auto held = std::ranges::find(_subscriptions, token, &Subscribed::Token);
    if (held == _subscriptions.end())
        return;

    held->Remove();
    _subscriptions.erase(held);
}

Unreleased HostView::RemoveAll()
{
    Unreleased unreleased;
    // Subscriptions first, so the plugin on its way out is not told about its own withdrawals.
    for (const Subscribed& held : _subscriptions)
    {
        held.Remove();
        unreleased.Subscriptions.push_back(held.Event);
    }
    _subscriptions.clear();

    unreleased.Services = _state.Services.RemoveAll(this);
    _state.Commands.RemoveAll(this);
    return unreleased;
}

}  // namespace VoltMod
