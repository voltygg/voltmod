#include "Engine/ConVarTypes.hpp"

#include <VoltMod/Engine/ConVars.hpp>
#include <format>
#include <string>
#include <string_view>
#include <tier1/convar.h>
#include <type_traits>

namespace VoltMod
{

// Keep the SDK-independent type mirror aligned with the engine.
static_assert(static_cast<int>(ConVarType::Bool) == EConVarType_Bool);
static_assert(static_cast<int>(ConVarType::Int32) == EConVarType_Int32);
static_assert(static_cast<int>(ConVarType::Float32) == EConVarType_Float32);
static_assert(static_cast<int>(ConVarType::String) == EConVarType_String);
static_assert(static_cast<int>(ConVarType::VectorWS) == EConVarType_VectorWS);

template <class T>
Result<ConVar<T>> ConVars::Find(std::string_view name)
{
    const std::string owned(name);
    ConVarRefAbstract ref(owned.c_str());
    if (!ref.IsValidRef() || !ref.IsConVarDataAvailable())
        return std::unexpected(Error::NotFound(std::format("no convar '{}'", owned)));

    const auto type = static_cast<ConVarType>(ref.GetType());
    if (!ConVarTypeMatches<T>(type))
        return std::unexpected(Error::Invalid(
            std::format("convar '{}' is engine type {}, not the requested one", owned, static_cast<int>(type))));

    // Prefer shared storage; some convars expose only slot 0.
    CVValue_t* storage = ref.GetConVarData()->Value(CSplitScreenSlot(-1));
    if (!storage)
        storage = ref.GetConVarData()->Value(CSplitScreenSlot(0));
    if (!storage)
        return std::unexpected(Error::Engine(std::format("convar '{}' has no value storage", owned)));

    ConVar<T> handle;
    handle._service = this;
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
        return value->m_fl32Value;
    }
    else
    {
        // Read the declared width to avoid adjacent bytes.
        switch (type)
        {
        case ConVarType::Int16:
            return value->m_i16Value;
        case ConVarType::UInt16:
            return value->m_u16Value;
        default:
            return value->m_i32Value;
        }
    }
}

template <class T>
Status ConVar<T>::Set(const T& value)
{
    if (!_storage || !_service)
        return std::unexpected(Error::NotReady("convar handle is unresolved"));

    return _service->ExecuteServerCommand(std::format("{} {}", _name, ConVarText(value)));
}

template <class T>
Status ConVar<T>::SetRaw(const T& value)
    requires(!std::is_same_v<T, std::string>)
{
    if (!_storage)
        return std::unexpected(Error::NotReady("convar handle is unresolved"));

    auto* storage = static_cast<CVValue_t*>(_storage);
    const auto type = static_cast<ConVarType>(_type);
    if constexpr (std::is_same_v<T, bool>)
    {
        storage->m_bValue = value;
    }
    else if constexpr (std::is_same_v<T, float>)
    {
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
        default:
            storage->m_i32Value = value;
            break;
        }
    }
    return {};
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
ConVarRawScope<T>::ConVarRawScope(ConVar<T>& cvar, const T& value)
    requires(!std::is_same_v<T, std::string>)
    : _cvar(&cvar), _previous(cvar.Get())
{
    if (!cvar.SetRaw(value))
        _cvar = nullptr;
}

template <class T>
ConVarRawScope<T>::~ConVarRawScope()
{
    if (_cvar)
        (void)_cvar->SetRaw(_previous);
}

// The complete set of supported handle types: the templates above are defined only here, so a `T`
// missing from this list is a link error at the consumer, not a silently different handle.
template class ConVar<bool>;
template class ConVar<int>;
template class ConVar<float>;
template class ConVar<std::string>;
template class ConVarRawScope<bool>;
template class ConVarRawScope<int>;
template class ConVarRawScope<float>;
template Result<ConVar<bool>> ConVars::Find<bool>(std::string_view);
template Result<ConVar<int>> ConVars::Find<int>(std::string_view);
template Result<ConVar<float>> ConVars::Find<float>(std::string_view);
template Result<ConVar<std::string>> ConVars::Find<std::string>(std::string_view);

}  // namespace VoltMod
