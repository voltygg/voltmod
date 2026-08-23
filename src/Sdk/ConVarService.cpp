#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/ConVarService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/RecipientFilter.hpp>
#include <engine/igameeventsystem.h>
#include <icvar.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <tier1/convar.h>

namespace
{

void GlobalConVarChangeCallback(ConVarRefAbstract* ref, CSplitScreenSlot /*slot*/, const char* newValue,
                                const char* oldValue, void* /*unk*/)
{
    if (!ref)
        return;

    const char* name = ref->GetName();
    CS2Kit::Detail::Rt().ConVars.DispatchChange(name, oldValue, newValue);
}

}  // namespace

namespace CS2Kit::Sdk
{
using namespace CS2Kit::Core;

RawConVar::RawConVar(const char* name)
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return;

    // Slot -1 is the shared (non-splitscreen) storage; some cvars only expose slot 0.
    CVValue_t* value = ref.GetConVarData()->Value(CSplitScreenSlot(-1));
    if (!value)
        value = ref.GetConVarData()->Value(CSplitScreenSlot(0));
    _value = value;
}

bool RawConVar::GetBool() const
{
    return _value && static_cast<CVValue_t*>(_value)->m_bValue;
}

void RawConVar::SetBool(bool value)
{
    if (_value)
        static_cast<CVValue_t*>(_value)->m_bValue = value;
}

float RawConVar::GetFloat() const
{
    return _value ? static_cast<CVValue_t*>(_value)->m_fl32Value : 0.0f;
}

void RawConVar::SetFloat(float value)
{
    if (_value)
        static_cast<CVValue_t*>(_value)->m_fl32Value = value;
}

bool ConVarService::Initialize()
{
    if (!CS2Kit::Detail::Rt().Interfaces.CVar)
    {
        Log::Error("ConVarService: ICvar not available.");
        return false;
    }

    Log::Info("ConVar service initialized.");
    return true;
}

std::optional<int> ConVarService::GetInt(const char* name) const
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    return ref.GetInt();
}

std::optional<float> ConVarService::GetFloat(const char* name) const
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    return ref.GetFloat();
}

std::optional<std::string> ConVarService::GetString(const char* name) const
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    CUtlString str = ref.GetString();
    return std::string(str.Get());
}

std::optional<bool> ConVarService::GetBool(const char* name) const
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    return ref.GetBool();
}

bool ConVarService::Exists(const char* name) const
{
    ConVarRefAbstract ref(name);
    return ref.IsValidRef() && ref.IsConVarDataAvailable();
}

bool ConVarService::SetInt(const char* name, int value)
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return false;

    ref.SetInt(value);
    return true;
}

bool ConVarService::SetFloat(const char* name, float value)
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return false;

    ref.SetFloat(value);
    return true;
}

bool ConVarService::SetString(const char* name, const char* value)
{
    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return false;

    ref.SetString(CUtlString(value));
    return true;
}

void ConVarService::ExecuteServerCommand(const char* command)
{
    auto* engine = CS2Kit::Detail::Rt().Interfaces.Engine;
    if (!engine)
    {
        Log::Warn("ConVarService::ExecuteServerCommand: IVEngineServer2 not available.");
        return;
    }

    if (!command)
        return;

    // ServerCommand appends to the shared command buffer verbatim, with no separator between
    // calls - so back-to-back commands would concatenate into one malformed line. Guarantee a
    // trailing newline so each call is parsed as its own console line.
    std::string line(command);
    if (line.empty() || line.back() != '\n')
        line.push_back('\n');

    engine->ServerCommand(line.c_str());
}

bool ConVarService::ReplicateToClient(int slot, const char* name, const char* value)
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;
    if (!interfaces.GameEventSystem || !interfaces.NetworkMessages || !Core::IsValidSlot(slot) || !name || !value)
        return false;

    if (!_setConVarMsg)
    {
        _setConVarMsg = interfaces.NetworkMessages->FindNetworkMessage("CNETMsg_SetConVar");
        if (!_setConVarMsg)
            _setConVarMsg = interfaces.NetworkMessages->FindNetworkMessagePartial("SetConVar");
    }

    if (!_setConVarMsg)
    {
        Log::Warn("ConVarService::ReplicateToClient: CNETMsg_SetConVar not found.");
        return false;
    }

    CNetMessage* pMsg = _setConVarMsg->AllocateMessage();
    if (!pMsg)
        return false;

    auto* pSetConVar = pMsg->ToPB<CNETMsg_SetConVar>();
    if (!pSetConVar)
    {
        interfaces.NetworkMessages->DeallocateNetMessageAbstract(_setConVarMsg, pMsg);
        return false;
    }

    auto* cvar = pSetConVar->mutable_convars()->add_cvars();
    cvar->set_name(name);
    cvar->set_value(value);

    SingleRecipientFilter filter(slot);
    interfaces.GameEventSystem->PostEventAbstract(-1, false, &filter, _setConVarMsg, pMsg, 0);

    interfaces.NetworkMessages->DeallocateNetMessageAbstract(_setConVarMsg, pMsg);
    return true;
}

Core::Subscription ConVarService::OnChange(ChangeCallback callback)
{
    if (!_globalCallbackInstalled)
    {
        auto* cvar = CS2Kit::Detail::Rt().Interfaces.CVar;
        if (cvar)
        {
            cvar->InstallGlobalChangeCallback(&GlobalConVarChangeCallback);
            _globalCallbackInstalled = true;
        }
    }

    return _changeCallbacks.AddOwned(std::move(callback));
}

void ConVarService::DispatchChange(const char* name, const char* oldValue, const char* newValue)
{
    for (const auto& [id, callback] : _changeCallbacks.Items())
    {
        callback(name, oldValue, newValue);
    }
}

}  // namespace CS2Kit::Sdk
