#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod
{

/** Hashes std::string and std::string_view alike, so a @ref StringMap looks up a view without a copy. */
struct StringHash
{
    using is_transparent = void;
    std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

/** A string-keyed map that finds a std::string_view key without building a string. */
template <class Value>
using StringMap = std::unordered_map<std::string, Value, StringHash, std::equal_to<>>;

}  // namespace VoltMod
