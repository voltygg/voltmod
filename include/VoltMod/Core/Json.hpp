#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <format>
#include <fstream>
#include <glaze/json.hpp>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>

/** Set by this header so RootApiSurfaceTest can assert `<VoltMod/Api.hpp>` never reaches the JSON
 *  layer. Glaze exposes no preprocessor version macro, so a first-party sentinel is the only
 *  guard that cannot rot. */
#define VOLTMOD_JSON_INCLUDED 1

namespace VoltMod
{

/**
 * @brief System.Text.Json-style helpers for mapping C++ types to and from JSON.
 *
 * Glaze reflects public aggregate members directly: the member name is the JSON key, a missing
 * key keeps the member's C++ initializer, and an **unknown key is an error** - a misspelled
 * setting fails the load instead of silently reading a default. No registration macro is needed.
 *
 * A settings root that carries the editor-completion `"$schema"` key needs
 * @ref VOLTMOD_SETTINGS_ROOT (`<VoltMod/App/PluginSettings.hpp>`) to accept and ignore it.
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
    /** JSONC (comments tolerated) with strict keys, which is what a settings file wants. */
    static constexpr glz::opts ReadOptions{.comments = true};

    /**
     * @brief Parse @p path (resolved via ResolvePath) into T.
     *
     * @return ErrorCode::NotFound when the file cannot be opened; ErrorCode::Invalid with a
     *         position-aware message (line, column, and the offending key) for a syntax error,
     *         a wrong-typed value, an unknown key, or invalid UTF-8.
     */
    template <class T>
    static Result<T> ReadFile(std::string_view path)
    {
        const auto resolved = ResolvePath(path);
        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open())
            return std::unexpected(Error::NotFound(std::format("failed to open {}", resolved.string())));

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto parsed = Read<T>(text);
        if (!parsed)
            return std::unexpected(Error::Invalid(std::format("{}: {}", path, parsed.error().Detail)));
        return parsed;
    }

    /** @brief As @ref ReadFile, over text already in hand. */
    template <class T>
    static Result<T> Read(std::string_view text)
    {
        T value{};
        if (auto ec = glz::read<ReadOptions>(value, text))
            return std::unexpected(Error::Invalid(glz::format_error(ec, text)));
        return value;
    }

    /** @brief @p value as compact JSON. Empty on the write errors reflected aggregates cannot hit. */
    template <class T>
    static std::string Write(const T& value)
    {
        auto written = glz::write_json(value);
        if (!written)
        {
            Log::Error("Json: failed to serialize: {}", glz::format_error(written.error()));
            return {};
        }
        return std::move(*written);
    }

    /**
     * @brief Parse free-form JSON whose shape belongs to an operator or a third-party service.
     *
     * UTF-8 is deliberately **not** validated here: the bytes come from a remote body or an
     * operator-authored template, and rejecting them would turn a soft failure into a hard one.
     * @ref ReadFile keeps validation on, because a config file with a bad byte is our problem.
     */
    static Result<glz::generic> ParseDocument(std::string_view text)
    {
        glz::generic document{};
        if (auto ec = glz::read<DocumentOptions>(document, text))
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

private:
    /** @ref ParseDocument's options; see the note there on why UTF-8 validation is off. */
    struct DocumentOpts : glz::opts
    {
        bool validate_utf8 = false;
    };
    static constexpr DocumentOpts DocumentOptions{};
};

}  // namespace VoltMod

/**
 * @brief Accept and ignore the editor-completion `"$schema"` key on a JSON document root.
 *
 * Unknown keys are an error, which is what makes a misspelled setting fail the load - but every
 * shipped `settings.jsonc` (and `gamedata.jsonc`) names its schema for editor completion, and
 * `$schema` is not a valid C++ identifier, so it cannot simply be a member. Write this at global
 * scope beside the struct; `glz::meta` is a library customization point and has to be specialized
 * outside `VoltMod`.
 *
 * @code
 * struct Settings { StandardPluginSettings plugin; MySection mine; };
 * VOLTMOD_SETTINGS_ROOT(MyPlugin::Settings)
 * @endcode
 */
#define VOLTMOD_SETTINGS_ROOT(Type)                                         \
    template <>                                                             \
    struct glz::meta<Type>                                                  \
    {                                                                       \
        static constexpr auto modify = glz::object("$schema", glz::skip{}); \
    };
