#pragma once

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <cstdint>
#include <format>
#include <glaze/json.hpp>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief System.Text.Json-style helpers for mapping C++ types to and from JSON.
 *
 * Glaze reflects public aggregate members directly: the member name is the JSON key and a missing
 * key keeps the member's C++ initializer. Unknown keys are ignored for compatibility with older
 * settings files. No registration macro is needed.
 *
 * @code
 * struct Cfg { std::string host = "localhost"; int port = 5432; };
 * auto cfg = Json::ReadFile<Cfg>("addons/my/config.jsonc");
 * if (!cfg)
 *     Log::Error("{}", cfg.error().Detail);
 * @endcode
 */
class Json
{
public:
    /** JSONC with the tolerant unknown-key behavior used by existing settings files. */
    static constexpr glz::opts ReadOptions{.comments = true, .error_on_unknown_keys = false};
    /** JSONC for formats whose schema rejects unknown keys. */
    static constexpr glz::opts StrictReadOptions{.comments = true};

    /**
     * @brief Parse @p path (resolved via ResolvePath) into T.
     *
     * @return ErrorCode::NotFound when the file cannot be opened; ErrorCode::Invalid with a
     *         position-aware message for a syntax error, wrong-typed value, or invalid UTF-8.
     */
    template <class T, auto Options = ReadOptions>
    static Result<T> ReadFile(std::string_view path)
    {
        auto text = ReadAllText(path);
        if (!text)
            return std::unexpected(text.error());

        auto parsed = Read<T, Options>(*text);
        if (!parsed)
            return std::unexpected(Error::Invalid(std::format("{}: {}", path, parsed.error().Detail)));
        return parsed;
    }

    /** @brief As @ref ReadFile, over text already in hand. */
    template <class T, auto Options = ReadOptions>
    static Result<T> Read(std::string_view text)
    {
        T value{};
        if (auto ec = glz::read<Options>(value, text))
            return std::unexpected(Error::Invalid(glz::format_error(ec, text)));
        return value;
    }

    /** Glaze reads `indentation_width` off the options type only when it declares one, so the
     *  two-space indentation this repo's JSON files use needs its own options struct. */
    struct PrettyOpts : glz::opts
    {
        uint8_t indentation_width;
    };
    static constexpr PrettyOpts PrettyOptions{{.prettify = true}, 2};

    /**
     * @brief Serialize @p value as prettified JSON.
     *
     * For a file a human reviews: one value per line keeps a `git diff` of a regenerated
     * artifact down to the values that actually changed.
     */
    template <class T>
    static std::string WritePretty(const T& value)
    {
        return Write<T, PrettyOptions>(value);
    }

    /** @brief Serialize @p value as compact JSON. */
    template <class T, auto Options = glz::opts{}>
    static std::string Write(const T& value)
    {
        auto written = glz::write<Options>(value);
        if (!written)
        {
            Log::Error("Json: failed to serialize: {}", glz::format_error(written.error()));
            return {};
        }
        return std::move(*written);
    }

    /** @brief Parse free-form JSON whose shape is not known at compile time. */
    static Result<glz::generic> ParseDocument(std::string_view text)
    {
        glz::generic document{};
        if (auto ec = glz::read_json(document, text))
            return std::unexpected(Error::Invalid(glz::format_error(ec, text)));
        return document;
    }

    /** @brief Recursively replace `{key}` tokens in every string value of @p node.
     *
     *  Substituting inside the parsed document rather than in its text is what keeps a token
     *  value containing `"` or `\` (a player name, say) from producing invalid JSON. */
    static void SubstituteTokens(glz::generic& node, const std::map<std::string, std::string>& tokens)
    {
        if (node.is_string())
        {
            node = Strings::SubstituteTokens(node.get_string(), tokens);
        }
        else if (node.is_object())
        {
            for (auto& [key, child] : node.get_object())
                SubstituteTokens(child, tokens);
        }
        else if (node.is_array())
        {
            for (auto& child : node.get<glz::generic::array_t>())
                SubstituteTokens(child, tokens);
        }
    }

    /**
     * @brief Descend a dot-separated path (e.g. "data.room.code") in @p jsonText.
     *
     * @return The leaf as a string, or "" when @p jsonText does not parse, the path is absent, or
     *         the leaf is neither a string nor a primitive. A numeric or boolean leaf reads as
     *         "42" / "true" without quotes.
     */
    static std::string GetStringByPath(std::string_view jsonText, std::string_view dotPath)
    {
        auto document = ParseDocument(jsonText);
        if (!document)
            return {};

        const glz::generic* node = &*document;
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
            return node->get_string();
        // A numeric value (e.g. a room id) is valid; dump() yields "42"/"true" without quotes.
        if (node->is_number() || node->is_boolean())
        {
            if (auto dumped = node->dump())
                return std::move(*dumped);
        }
        return {};
    }
};

}  // namespace VoltMod
