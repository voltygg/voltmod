#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/ConVars/ConVar.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Net/RecipientFilter.hpp>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <string_view>
#include <tier1/convar.h>

// ICvar provides no callback context. Each plugin DLL owns one ConVars instance and callback.
static VoltMod::ConVars* g_changeCallback = nullptr;

static std::string_view Text(const char* value)
{
    return value ? std::string_view(value) : std::string_view{};
}

static void GlobalConVarChangeCallback(ConVarRefAbstract* ref, CSplitScreenSlot /*slot*/, const char* newValue,
                                       const char* oldValue, void* /*unk*/)
{
    if (!ref || !g_changeCallback)
    {
        return;
    }

    g_changeCallback->Changed.Raise(
        VoltMod::ConVarChange{.Name = Text(ref->GetName()), .OldValue = Text(oldValue), .NewValue = Text(newValue)});
}

namespace VoltMod
{

ConVars::ConVars(Interfaces& interfaces)
    : Changed({.OnFirst = [this] { return RouteChanges(); }, .OnLast = [this] { StopRoutingChanges(); }}),
      _interfaces(interfaces)
{}

ConVars::~ConVars()
{
    // Guard against subscriptions that outlive the service.
    StopRoutingChanges();
}

Status ConVars::Initialize()
{
    if (!_interfaces.CVar)
    {
        return std::unexpected(Error::NotReady("ICvar not available"));
    }

    Log::Info("ConVar service initialized.");
    return {};
}

void ConVars::ExecuteServerCommand(std::string_view command)
{
    auto* engine = _interfaces.Engine;
    if (!engine)
    {
        return;
    }

    // A newline cannot be quoted away - it ends the line and whatever follows runs as its
    // own command - so an injected one is refused rather than escaped.
    if (command.find_first_of("\r\n") != std::string_view::npos)
    {
        Log::Warn("ConVars: refused a server command with an embedded newline.");
        return;
    }

    // ServerCommand adds no separator between buffered commands.
    std::string line(command);
    if (line.empty() || line.back() != '\n')
    {
        line.push_back('\n');
    }

    engine->ServerCommand(line.c_str());
}

void ConVars::ExecuteClientCommand(int slot, std::string_view command)
{
    auto* cvar = _interfaces.CVar;
    auto* clients = _interfaces.ServerGameClients;
    if (!cvar || !clients || !IsValidSlot(slot))
    {
        return;
    }

    CCommand args;
    if (command.empty() || command.find_first_of(";\r\n") != std::string_view::npos ||
        !args.Tokenize(CUtlString(std::string(command).c_str())) || args.ArgC() == 0)
    {
        Log::Warn("ConVars: refused client command '{}'; it must be one non-empty command.", command);
        return;
    }

    ConCommandRef registered = cvar->FindConCommand(args.Arg(0));
    if (registered.IsValidRef())
    {
        cvar->DispatchConCommand(registered, CCommandContext(CT_FIRST_SPLITSCREEN_CLIENT, CPlayerSlot(slot)), args);
    }
    else
    {
        clients->ClientCommand(CPlayerSlot(slot), args);
    }
}

void ConVars::SetByConsole(std::string_view name, std::string_view value)
{
    ExecuteServerCommand(std::format("{} {}", name, Strings::QuoteConsoleArg(value)));
}

INetworkMessageInternal* ConVars::SetConVarMessage()
{
    if (_setConVarMsg)
    {
        return _setConVarMsg;
    }

    auto* messages = _interfaces.NetworkMessages;
    if (!messages)
    {
        return nullptr;
    }

    _setConVarMsg = messages->FindNetworkMessagePartial("SetConVar");
    if (!_setConVarMsg)
    {
        Log::Warn("ConVars: CNETMsg_SetConVar not found; per-client convar overrides are unavailable.");
    }

    return _setConVarMsg;
}

bool ConVars::SendToClient(int slot, std::string_view name, std::string_view value)
{
    if (!_interfaces.GameEventSystem || !IsValidSlot(slot) || name.empty())
    {
        return false;
    }

    auto* msgType = SetConVarMessage();
    if (!msgType)
    {
        return false;
    }

    CNetMessage* msg = msgType->AllocateMessage();
    if (!msg)
    {
        return false;
    }

    auto* setConVar = msg->ToPB<CNETMsg_SetConVar>();
    if (setConVar)
    {
        auto* cvar = setConVar->mutable_convars()->add_cvars();
        cvar->set_name(std::string(name));
        cvar->set_value(std::string(value));

        SingleRecipientFilter filter(slot);
        _interfaces.GameEventSystem->PostEventAbstract(-1, false, &filter, msgType, msg, 0);
    }

    _interfaces.NetworkMessages->DeallocateNetMessageAbstract(msgType, msg);
    return setConVar != nullptr;
}

bool ConVars::RouteChanges()
{
    if (_routingChanges)
    {
        return true;
    }

    auto* cvar = _interfaces.CVar;
    if (!cvar)
    {
        Log::Warn("ConVars: ICvar is not resolved; convar change handlers will not fire.");
        return false;
    }

    cvar->InstallGlobalChangeCallback(&GlobalConVarChangeCallback);
    g_changeCallback = this;
    _routingChanges = true;
    return true;
}

void ConVars::StopRoutingChanges()
{
    if (!_routingChanges)
    {
        return;
    }

    if (auto* cvar = _interfaces.CVar)
    {
        cvar->RemoveGlobalChangeCallback(&GlobalConVarChangeCallback);
    }

    g_changeCallback = nullptr;
    _routingChanges = false;
}

}  // namespace VoltMod
