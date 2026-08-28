#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief System.Text.Json-style helpers for mapping C++ types to and from JSON.
 *
 * Works with any type that has nlohmann `to_json`/`from_json` - easiest via the
 * `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` macro, which keys on member names
 * (they must match the JSON keys) and defaults any missing key. The `Try*` variants
 * catch and log instead of throwing; a missing key is fine, a wrong-typed value is not.
 *
 * @code
 * struct Cfg { std::string host = "localhost"; int port = 5432; };
 * NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Cfg, host, port)
 * auto cfg = Json::TryDeserializeFile<Cfg>("addons/my/config.json");
 * @endcode
 */
class Json
{
public:
    /** @brief Parse a JSON file into T (path resolved via ResolvePath). Returns nullopt and logs on any error. */
    template <typename T>
    static std::optional<T> TryDeserializeFile(std::string_view path)
    {
        try
        {
            auto resolved = ResolvePath(path);
            std::ifstream file(resolved);
            if (!file.is_open())
            {
                Log::Error("Json: failed to open {}", resolved.string());
                return std::nullopt;
            }

            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto j = nlohmann::json::parse(text, nullptr, true, /*ignore_comments=*/true);
            return j.template get<T>();
        }
        catch (const std::exception& e)
        {
            Log::Error("Json: error reading {}: {}", path, e.what());
            return std::nullopt;
        }
    }

    /** @brief Recursively replace `{key}` tokens in every string value of @p node (objects/arrays descended). */
    static void SubstituteTokens(nlohmann::json& node, const std::map<std::string, std::string>& tokens)
    {
        if (node.is_string())
            node = Strings::SubstituteTokens(node.get<std::string>(), tokens);
        else if (node.is_structured())
            for (auto& child : node)
                SubstituteTokens(child, tokens);
    }

    /** @brief Descend a dot-separated path (e.g. "data.room.code") and return the leaf as a string; "" if absent. */
    static std::string GetStringByPath(const nlohmann::json& root, std::string_view dotPath)
    {
        const nlohmann::json* node = &root;
        for (size_t start = 0; start <= dotPath.size();)
        {
            const size_t dot = dotPath.find('.', start);
            const std::string key(
                dotPath.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
            if (!node->is_object() || !node->contains(key))
                return {};
            node = &(*node)[key];
            if (dot == std::string_view::npos)
                break;
            start = dot + 1;
        }

        if (node->is_string())
            return node->get<std::string>();
        // A numeric value (e.g. a room id) is valid; dump() yields "42"/"true" without quotes.
        if (node->is_primitive() && !node->is_null())
            return node->dump();
        return {};
    }
};

}  // namespace VoltMod
