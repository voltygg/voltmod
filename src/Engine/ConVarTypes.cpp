#include "Engine/ConVarTypes.hpp"

#include <VoltMod/Engine/ConVars.hpp>
#include <format>
#include <string>
#include <type_traits>

namespace VoltMod
{

template <class T>
bool ConVarTypeMatches(ConVarType type)
{
    // Only widths that round-trip through T. Accepting UInt32/Int64/UInt64 as int, or Float64
    // as float, made Find() succeed and then silently narrow on every Get - a stronger promise
    // than the handle could keep. A convar of an unrepresentable width now fails to resolve,
    // with the type mismatch named, instead of reading wrong.
    if constexpr (std::is_same_v<T, bool>)
        return type == ConVarType::Bool;
    else if constexpr (std::is_same_v<T, int>)
        return type == ConVarType::Int16 || type == ConVarType::UInt16 || type == ConVarType::Int32;
    else if constexpr (std::is_same_v<T, float>)
        return type == ConVarType::Float32;
    else
        return type == ConVarType::String;
}

template bool ConVarTypeMatches<bool>(ConVarType);
template bool ConVarTypeMatches<int>(ConVarType);
template bool ConVarTypeMatches<float>(ConVarType);
template bool ConVarTypeMatches<std::string>(ConVarType);

template <>
std::string ConVarText<bool>(const bool& value)
{
    return value ? "1" : "0";
}

template <>
std::string ConVarText<int>(const int& value)
{
    return std::format("{}", value);
}

template <>
std::string ConVarText<float>(const float& value)
{
    return std::format("{}", value);
}

template <>
std::string ConVarText<std::string>(const std::string& value)
{
    return value;
}

}  // namespace VoltMod
