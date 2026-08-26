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

namespace VoltMod::Core
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
    /** @brief Parse JSON text into T. Tolerates JSONC comments. Throws on malformed JSON or a type mismatch. */
    template <typename T>
    static T Deserialize(std::string_view text)
    {
        return nlohmann::json::parse(text, nullptr, true, /*ignore_comments=*/true).template get<T>();
    }

    /** @brief Serialize a value to a JSON string. @p pretty enables 2-space indentation. */
    template <typename T>
    static std::string Serialize(const T& value, bool pretty = false)
    {
        const nlohmann::json j = value;
        return pretty ? j.dump(2) : j.dump();
    }

    /** @brief Parse a JSON file into T (path resolved via ResolvePath). Returns nullopt and logs on any error. */
    template <typename T>
    static std::optional<T> TryDeserializeFile(const std::string& path)
    {
        try
        {
            auto resolved = Core::ResolvePath(path);
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

    /** @brief Write a value to a JSON file (path resolved via ResolvePath). Returns false and logs on failure. */
    template <typename T>
    static bool SerializeToFile(const std::string& path, const T& value, bool pretty = true)
    {
        try
        {
            auto resolved = Core::ResolvePath(path);
            std::ofstream file(resolved);
            if (!file.is_open())
            {
                Log::Error("Json: failed to open {} for writing", resolved.string());
                return false;
            }

            const nlohmann::json j = value;
            file << j.dump(pretty ? 2 : -1);
            return true;
        }
        catch (const std::exception& e)
        {
            Log::Error("Json: error writing {}: {}", path, e.what());
            return false;
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

}  // namespace VoltMod::Core
