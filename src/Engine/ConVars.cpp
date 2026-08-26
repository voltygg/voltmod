#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <algorithm>
#include <engine/igameeventsystem.h>
#include <icvar.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <tier1/convar.h>
#include <vector>

namespace
{

/**
 * Every ConVars currently routing engine changes to its listeners.
 *
 * ICvar takes a bare function pointer with no user data, so the trampoline below has nothing to
 * carry a service reference in. It is a list rather than one pointer because each plugin owns its
 * own Runtime, and therefore its own ConVars: a single slot would let whichever plugin
 * subscribed last silently cut off the others. The engine callback is installed when this list
 * becomes non-empty and removed when it empties, so it is installed exactly once.
 */
std::vector<VoltMod::Engine::ConVars*> g_changeSinks;

void GlobalConVarChangeCallback(ConVarRefAbstract* ref, CSplitScreenSlot /*slot*/, const char* newValue,
                                const char* oldValue, void* /*unk*/)
{
    if (!ref)
        return;

    // Copied because a listener may subscribe or shut down while this loop runs.
    const auto sinks = g_changeSinks;
    for (auto* sink : sinks)
        sink->DispatchChange(ref->GetName(), oldValue, newValue);
}

/** Resolve @p name to a usable convar reference, or nullopt when it is null or not registered. */
std::optional<ConVarRefAbstract> Resolve(const char* name)
{
    if (!name)
        return std::nullopt;

    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    return ref;
}

}  // namespace

namespace VoltMod::Engine
{
using namespace VoltMod::Core;

ConVars::ConVars(Interfaces& interfaces) : _interfaces(interfaces) {}

ConVars::~ConVars()
{
    Shutdown();
}

ConVarStorage::ConVarStorage(const char* name)
{
    auto ref = Resolve(name);
    if (!ref)
        return;

    // Slot -1 is the shared (non-splitscreen) storage; some cvars only expose slot 0.
    CVValue_t* value = ref->GetConVarData()->Value(CSplitScreenSlot(-1));
    if (!value)
        value = ref->GetConVarData()->Value(CSplitScreenSlot(0));
    _value = value;
}

bool ConVarStorage::GetBool() const
{
    return _value && static_cast<CVValue_t*>(_value)->m_bValue;
}

void ConVarStorage::SetBool(bool value)
{
    if (_value)
        static_cast<CVValue_t*>(_value)->m_bValue = value;
}

float ConVarStorage::GetFloat() const
{
    return _value ? static_cast<CVValue_t*>(_value)->m_fl32Value : 0.0f;
}

void ConVarStorage::SetFloat(float value)
{
    if (_value)
        static_cast<CVValue_t*>(_value)->m_fl32Value = value;
}

bool ConVars::Initialize()
{
    if (!_interfaces.CVar)
    {
        Log::Error("ConVars: ICvar not available.");
        return false;
    }

    Log::Info("ConVar service initialized.");
    return true;
}

std::optional<int> ConVars::GetInt(const char* name) const
{
    auto ref = Resolve(name);
    return ref ? std::optional(ref->GetInt()) : std::nullopt;
}

std::optional<float> ConVars::GetFloat(const char* name) const
{
    auto ref = Resolve(name);
    return ref ? std::optional(ref->GetFloat()) : std::nullopt;
}

std::optional<std::string> ConVars::GetString(const char* name) const
{
    auto ref = Resolve(name);
    if (!ref)
        return std::nullopt;

    CUtlString str = ref->GetString();
    return std::string(str.Get());
}

std::optional<bool> ConVars::GetBool(const char* name) const
{
    auto ref = Resolve(name);
    return ref ? std::optional(ref->GetBool()) : std::nullopt;
}

bool ConVars::Exists(const char* name) const
{
    return Resolve(name).has_value();
}

bool ConVars::SetInt(const char* name, int value)
{
    auto ref = Resolve(name);
    if (!ref)
        return false;

    ref->SetInt(value);
    return true;
}

bool ConVars::SetFloat(const char* name, float value)
{
    auto ref = Resolve(name);
    if (!ref)
        return false;

    ref->SetFloat(value);
    return true;
}

bool ConVars::SetString(const char* name, const char* value)
{
    auto ref = Resolve(name);
    if (!ref || !value)
        return false;

    ref->SetString(CUtlString(value));
    return true;
}

void ConVars::ExecuteServerCommand(std::string_view command)
{
    auto* engine = _interfaces.Engine;
    if (!engine)
    {
        Log::Warn("ConVars::ExecuteServerCommand: IVEngineServer2 not available.");
        return;
    }

    // ServerCommand appends to the shared command buffer verbatim, with no separator between
    // calls - so back-to-back commands would concatenate into one malformed line. Guarantee a
    // trailing newline so each call is parsed as its own console line.
    std::string line(command);
    if (line.empty() || line.back() != '\n')
        line.push_back('\n');

    engine->ServerCommand(line.c_str());
}

INetworkMessageInternal* ConVars::SetConVarMessage()
{
    if (_setConVarMsg)
        return _setConVarMsg;

    auto* messages = _interfaces.NetworkMessages;
    if (!messages)
        return nullptr;

    _setConVarMsg = messages->FindNetworkMessage("CNETMsg_SetConVar");
    if (!_setConVarMsg)
        _setConVarMsg = messages->FindNetworkMessagePartial("SetConVar");
    if (!_setConVarMsg)
        Log::Warn("ConVars::ReplicateToClient: CNETMsg_SetConVar not found.");

    return _setConVarMsg;
}

bool ConVars::ReplicateToClient(int slot, const char* name, const char* value)
{
    if (!_interfaces.GameEventSystem || !Core::IsValidSlot(slot) || !name || !value)
        return false;

    auto* msgType = SetConVarMessage();
    if (!msgType)
        return false;

    CNetMessage* msg = msgType->AllocateMessage();
    if (!msg)
        return false;

    auto* setConVar = msg->ToPB<CNETMsg_SetConVar>();
    if (setConVar)
    {
        auto* cvar = setConVar->mutable_convars()->add_cvars();
        cvar->set_name(name);
        cvar->set_value(value);

        SingleRecipientFilter filter(slot);
        _interfaces.GameEventSystem->PostEventAbstract(-1, false, &filter, msgType, msg, 0);
    }

    _interfaces.NetworkMessages->DeallocateNetMessageAbstract(msgType, msg);
    return setConVar != nullptr;
}

Core::Subscription ConVars::OnChange(ChangeCallback callback)
{
    if (!_routingChanges)
    {
        if (auto* cvar = _interfaces.CVar)
        {
            if (g_changeSinks.empty())
                cvar->InstallGlobalChangeCallback(&GlobalConVarChangeCallback);
            g_changeSinks.push_back(this);
            _routingChanges = true;
        }
    }

    return _changeCallbacks.AddOwned(std::move(callback));
}

void ConVars::Shutdown()
{
    _changeCallbacks.Clear();

    if (!_routingChanges)
        return;

    std::erase(g_changeSinks, this);
    _routingChanges = false;

    // The engine callback belongs to whichever services are still listening; take it off only
    // once the last one is gone, or an unloading plugin would deafen the others.
    if (g_changeSinks.empty())
    {
        if (auto* cvar = _interfaces.CVar)
            cvar->RemoveGlobalChangeCallback(&GlobalConVarChangeCallback);
    }
}

void ConVars::DispatchChange(const char* name, const char* oldValue, const char* newValue)
{
    _changeCallbacks.Dispatch([&](auto& callback) { callback(name, oldValue, newValue); });
}

}  // namespace VoltMod::Engine
