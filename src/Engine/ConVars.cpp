#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <engine/igameeventsystem.h>
#include <icvar.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string_view>
#include <tier1/convar.h>

/**
 * The one ConVars currently routing engine changes, or null.
 *
 * ICvar takes a bare function pointer with no user data, so the trampoline below has nothing to
 * carry a service reference in. One slot is enough because this static, the trampoline, and the
 * Runtime that owns the service are all per plugin DLL: another plugin's ConVars installs its own
 * copy of this callback into the engine and never touches this one.
 */
static VoltMod::ConVars* g_changeSink = nullptr;

static std::string_view Text(const char* value)
{
    return value ? std::string_view(value) : std::string_view{};
}

static void GlobalConVarChangeCallback(ConVarRefAbstract* ref, CSplitScreenSlot /*slot*/, const char* newValue,
                                       const char* oldValue, void* /*unk*/)
{
    if (!ref || !g_changeSink)
        return;

    g_changeSink->Changed.Raise(
        VoltMod::ConVarChange{.Name = Text(ref->GetName()), .OldValue = Text(oldValue), .NewValue = Text(newValue)});
}

/** Resolve @p name to a usable convar reference, or nullopt when it is null or not registered. */
static std::optional<ConVarRefAbstract> Resolve(const char* name)
{
    if (!name)
        return std::nullopt;

    ConVarRefAbstract ref(name);
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::nullopt;

    return ref;
}

namespace VoltMod
{

ConVars::ConVars(Interfaces& interfaces)
    : Changed({.OnFirst = [this] { return RouteChanges(); }, .OnLast = [this] { StopRoutingChanges(); }}),
      _interfaces(interfaces)
{}

ConVars::~ConVars()
{
    // Belt and braces: the last Subscription dropping is what normally unhooks. This covers a
    // subscription that outlived the service, which would otherwise leave the engine calling a
    // trampoline into freed storage.
    StopRoutingChanges();
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
    if (!_interfaces.GameEventSystem || !IsValidSlot(slot) || !name || !value)
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

bool ConVars::RouteChanges()
{
    if (_routingChanges)
        return true;

    auto* cvar = _interfaces.CVar;
    if (!cvar)
    {
        Log::Warn("ConVars: ICvar is not resolved; convar change handlers will not fire.");
        return false;
    }

    cvar->InstallGlobalChangeCallback(&GlobalConVarChangeCallback);
    g_changeSink = this;
    _routingChanges = true;
    return true;
}

void ConVars::StopRoutingChanges()
{
    if (!_routingChanges)
        return;

    if (auto* cvar = _interfaces.CVar)
        cvar->RemoveGlobalChangeCallback(&GlobalConVarChangeCallback);

    g_changeSink = nullptr;
    _routingChanges = false;
}

}  // namespace VoltMod
