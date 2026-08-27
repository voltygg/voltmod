#include "Engine/ConVarTypes.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <optional>
#include <string>
#include <string_view>
#include <tier1/convar.h>
#include <type_traits>

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

// ConVarType mirrors EConVarType so the type check and the console rendering can be checked
// without the SDK. If the engine ever renumbers these, the mirror is what has to move.
static_assert(static_cast<int>(ConVarType::Bool) == EConVarType_Bool);
static_assert(static_cast<int>(ConVarType::Int32) == EConVarType_Int32);
static_assert(static_cast<int>(ConVarType::Float32) == EConVarType_Float32);
static_assert(static_cast<int>(ConVarType::String) == EConVarType_String);
static_assert(static_cast<int>(ConVarType::VectorWS) == EConVarType_VectorWS);

template <class T>
Result<ConVar<T>> ConVar<T>::Find(ConVars& service, std::string_view name)
{
    const std::string owned(name);
    auto ref = Resolve(owned.c_str());
    if (!ref)
        return std::unexpected(Error::NotFound(std::format("no convar '{}'", owned)));

    const auto type = static_cast<ConVarType>(ref->GetType());
    if (!ConVarTypeMatches<T>(type))
        return std::unexpected(Error::Invalid(
            std::format("convar '{}' is engine type {}, not the requested one", owned, static_cast<int>(type))));

    // Slot -1 is the shared (non-splitscreen) storage; some convars only expose slot 0.
    CVValue_t* storage = ref->GetConVarData()->Value(CSplitScreenSlot(-1));
    if (!storage)
        storage = ref->GetConVarData()->Value(CSplitScreenSlot(0));
    if (!storage)
        return std::unexpected(Error::Engine(std::format("convar '{}' has no value storage", owned)));

    ConVar<T> handle;
    handle._service = &service;
    handle._name = owned;
    handle._storage = storage;
    handle._type = static_cast<int16_t>(type);
    return handle;
}

template <class T>
T ConVar<T>::Get() const
{
    if (!_storage)
        return T{};

    const auto* value = static_cast<const CVValue_t*>(_storage);
    const auto type = static_cast<ConVarType>(_type);

    if constexpr (std::is_same_v<T, bool>)
    {
        return value->m_bValue;
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        const char* text = value->m_StringValue.Get();
        return text ? std::string(text) : std::string{};
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        return type == ConVarType::Float64 ? static_cast<float>(value->m_fl64Value) : value->m_fl32Value;
    }
    else
    {
        // Read the engine's own width; a 32-bit read of an int16 convar would pick up the
        // neighbouring two bytes.
        switch (type)
        {
        case ConVarType::Int16:
            return value->m_i16Value;
        case ConVarType::UInt16:
            return value->m_u16Value;
        case ConVarType::UInt32:
            return static_cast<int>(value->m_u32Value);
        case ConVarType::Int64:
            return static_cast<int>(value->m_i64Value);
        case ConVarType::UInt64:
            return static_cast<int>(value->m_u64Value);
        default:
            return value->m_i32Value;
        }
    }
}

template <class T>
Status ConVar<T>::Set(const T& value, SetMode mode)
{
    if (!_storage || !_service)
        return std::unexpected(Error::NotReady("convar handle is unresolved"));

    if (mode == SetMode::Console)
    {
        _service->ExecuteServerCommand(std::format("{} {}", _name, ConVarText(value)));
        return {};
    }

    // Raw: poke the storage the engine reads from, with nobody told.
    if constexpr (std::is_same_v<T, std::string>)
    {
        return std::unexpected(
            Error::Unsupported("raw writes are not supported for string convars (the value owns heap storage)"));
    }
    else
    {
        auto* storage = static_cast<CVValue_t*>(_storage);
        const auto type = static_cast<ConVarType>(_type);
        if constexpr (std::is_same_v<T, bool>)
        {
            storage->m_bValue = value;
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (type == ConVarType::Float64)
                storage->m_fl64Value = value;
            else
                storage->m_fl32Value = value;
        }
        else
        {
            switch (type)
            {
            case ConVarType::Int16:
                storage->m_i16Value = static_cast<int16_t>(value);
                break;
            case ConVarType::UInt16:
                storage->m_u16Value = static_cast<uint16_t>(value);
                break;
            case ConVarType::UInt32:
                storage->m_u32Value = static_cast<uint32_t>(value);
                break;
            case ConVarType::Int64:
                storage->m_i64Value = value;
                break;
            case ConVarType::UInt64:
                storage->m_u64Value = static_cast<uint64_t>(value);
                break;
            default:
                storage->m_i32Value = value;
                break;
            }
        }
        return {};
    }
}

template <class T>
Status ConVar<T>::SetFor(int slot, const T& value) const
{
    if (!_storage || !_service)
        return std::unexpected(Error::NotReady("convar handle is unresolved"));

    if (!_service->SendToClient(slot, _name, ConVarText(value)))
        return std::unexpected(Error::Engine(std::format("could not send '{}' to slot {}", _name, slot)));
    return {};
}

template <class T>
ConVarRawScope<T>::ConVarRawScope(ConVar<T>& cvar, const T& value) : _cvar(&cvar), _previous(cvar.Get())
{
    if (!cvar.Set(value, SetMode::Raw))
        _cvar = nullptr;  // nothing was written, so there is nothing to put back
}

template <class T>
ConVarRawScope<T>::~ConVarRawScope()
{
    if (_cvar)
        (void)_cvar->Set(_previous, SetMode::Raw);
}

// The four convar types the framework supports; every other T is a compile error at the call site.
template class ConVar<bool>;
template class ConVar<int>;
template class ConVar<float>;
template class ConVar<std::string>;
template class ConVarRawScope<bool>;
template class ConVarRawScope<int>;
template class ConVarRawScope<float>;
template class ConVarRawScope<std::string>;

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

Status ConVars::Initialize()
{
    if (!_interfaces.CVar)
        return std::unexpected(Error::NotReady("ICvar not available"));

    Log::Info("ConVar service initialized.");
    return {};
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
        Log::Warn("ConVars: CNETMsg_SetConVar not found; per-client convar overrides are unavailable.");

    return _setConVarMsg;
}

bool ConVars::SendToClient(int slot, std::string_view name, std::string_view value)
{
    if (!_interfaces.GameEventSystem || !IsValidSlot(slot) || name.empty())
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
