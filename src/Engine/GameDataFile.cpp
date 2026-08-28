#include "Engine/GameDataFile.hpp"

#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Engine/OffsetCheck.hpp>
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view PlatformKey(GamePlatform platform)
{
    return platform == GamePlatform::Windows ? "windows" : "linux";
}

static std::unexpected<Error> Malformed(std::string detail)
{
    return std::unexpected(Error::Invalid(std::move(detail)));
}

/** The section a key was first seen in, so a collision can say which two sections clash. */
static Status ClaimKey(std::map<std::string, std::string>& owners, const std::string& key, std::string_view section)
{
    auto [it, inserted] = owners.emplace(key, section);
    if (!inserted)
        return Malformed(std::format("'{}' is declared in both '{}' and '{}'", key, it->second, section));
    return {};
}

bool IsValidBytePattern(std::string_view pattern)
{
    // Mirrors gamedata.schema.json's regex: "AA BB ? ?? CC", at least one token.
    size_t tokens = 0;
    size_t i = 0;
    while (i < pattern.size())
    {
        if (pattern[i] == ' ')
        {
            // Exactly one separator: a leading, trailing or doubled space is malformed.
            if (tokens == 0 || i + 1 >= pattern.size() || pattern[i + 1] == ' ')
                return false;
            ++i;
            continue;
        }

        if (pattern[i] == '?')
        {
            ++i;
            if (i < pattern.size() && pattern[i] == '?')
                ++i;
        }
        else
        {
            size_t start = i;
            while (i < pattern.size() && std::isxdigit(static_cast<unsigned char>(pattern[i])))
                ++i;
            if (i - start != 2)
                return false;
        }

        // Whatever follows a token must be a separator.
        if (i < pattern.size() && pattern[i] != ' ')
            return false;
        ++tokens;
    }
    return tokens > 0;
}

/**
 * Whether @p entry carries the other platform's column, i.e. it is deliberately single-platform
 * rather than malformed. gamedata.schema.json requires one of the two columns, not both: something
 * only located on Windows so far is a capability that is off on Linux.
 */
static bool HasOtherPlatform(const nlohmann::json& entry, GamePlatform platform)
{
    const std::string_view other =
        PlatformKey(platform == GamePlatform::Windows ? GamePlatform::Linux : GamePlatform::Windows);
    return entry.is_object() && entry.contains(other);
}

/** Record @p key as unavailable here and tell the caller to skip it. See @ref HasOtherPlatform. */
static bool SkipOtherPlatform(const nlohmann::json& entry, GamePlatform platform, const std::string& key,
                              GameDataFile& out)
{
    if (!entry.is_object() || entry.contains(PlatformKey(platform)) || !HasOtherPlatform(entry, platform))
        return false;

    out.OtherPlatformOnly.push_back(key);
    return true;
}

/** Read one platform column of @p entry as an integer, or report which key has no column. */
static Result<int> PlatformInt(const nlohmann::json& entry, GamePlatform platform, std::string_view section,
                               const std::string& key)
{
    const std::string_view column = PlatformKey(platform);
    if (!entry.is_object())
        return Malformed(std::format("{}.{} is not an object", section, key));
    if (!entry.contains(column))
        return Malformed(std::format("{}.{} has no '{}' entry", section, key, column));
    if (!entry[column].is_number_integer())
        return Malformed(std::format("{}.{}.{} is not an integer", section, key, column));
    return entry[column].get<int>();
}

static Status ParseSignatures(const nlohmann::json& json, GamePlatform platform, GameDataFile& out,
                              std::map<std::string, std::string>& owners)
{
    if (!json.contains("signatures"))
        return {};
    if (!json["signatures"].is_object())
        return Malformed("'signatures' is not an object");

    const std::string_view column = PlatformKey(platform);
    for (const auto& [key, entry] : json["signatures"].items())
    {
        if (Status claimed = ClaimKey(owners, key, "signatures"); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("signatures.{} is not an object", key));

        if (SkipOtherPlatform(entry, platform, key, out))
            continue;
        if (!entry.contains(column))
            return Malformed(std::format("signatures.{} has no '{}' entry", key, column));
        if (!entry[column].is_object())
            return Malformed(std::format("signatures.{}.{} is not an object", key, column));

        SignatureEntry signature;
        signature.Library = entry.value("library", std::string("server"));
        signature.Pattern = entry[column].value("pattern", std::string{});
        if (!IsValidBytePattern(signature.Pattern))
            return Malformed(std::format("signatures.{}.{}.pattern is not a byte pattern", key, column));

        out.Signatures.emplace(key, std::move(signature));
    }
    return {};
}

static Status ParseAddresses(const nlohmann::json& json, GamePlatform platform, GameDataFile& out,
                             std::map<std::string, std::string>& owners)
{
    if (!json.contains("addresses"))
        return {};
    if (!json["addresses"].is_object())
        return Malformed("'addresses' is not an object");

    for (const auto& [key, entry] : json["addresses"].items())
    {
        if (Status claimed = ClaimKey(owners, key, "addresses"); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("addresses.{} is not an object", key));

        AddressEntry address;
        address.Signature = entry.value("signature", std::string{});
        // An address derived from a signature this platform does not carry goes with it, rather
        // than reading as a reference to a signature nobody wrote.
        if (std::ranges::contains(out.OtherPlatformOnly, address.Signature))
        {
            out.OtherPlatformOnly.push_back(key);
            continue;
        }
        if (!out.Signatures.contains(address.Signature))
            return Malformed(std::format("addresses.{} derives from unknown signature '{}'", key, address.Signature));

        if (!entry.contains("rel32At"))
            return Malformed(std::format("addresses.{} has no 'rel32At'", key));

        if (SkipOtherPlatform(entry["rel32At"], platform, key, out))
            continue;

        auto rel32At = PlatformInt(entry["rel32At"], platform, "addresses", key + ".rel32At");
        if (!rel32At)
            return std::unexpected(rel32At.error());
        if (*rel32At < 0)
            return Malformed(std::format("addresses.{}.rel32At is negative ({})", key, *rel32At));

        address.Rel32At = *rel32At;
        out.Addresses.emplace(key, std::move(address));
    }
    return {};
}

static Status ParseVTables(const nlohmann::json& json, GamePlatform platform, GameDataFile& out,
                           std::map<std::string, std::string>& owners)
{
    if (!json.contains("vtables"))
        return {};
    if (!json["vtables"].is_object())
        return Malformed("'vtables' is not an object");

    for (const auto& [key, entry] : json["vtables"].items())
    {
        if (Status claimed = ClaimKey(owners, key, "vtables"); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("vtables.{} is not an object", key));

        if (SkipOtherPlatform(entry, platform, key, out))
            continue;

        VTableEntry vtable;
        vtable.Class = entry.value("class", std::string{});
        if (vtable.Class.empty())
            return Malformed(std::format("vtables.{} has no 'class'", key));
        vtable.Library = entry.value("library", std::string("server"));

        auto index = PlatformInt(entry, platform, "vtables", key);
        if (!index)
            return std::unexpected(index.error());
        if (*index < 0 || *index >= MaxVtableIndex)
            return Malformed(std::format("vtables.{} index {} is outside [0, {})", key, *index, MaxVtableIndex));

        vtable.Index = *index;
        out.VTables.emplace(key, std::move(vtable));
    }
    return {};
}

static Status ParseOffsets(const nlohmann::json& json, GamePlatform platform, GameDataFile& out,
                           std::map<std::string, std::string>& owners)
{
    if (!json.contains("offsets"))
        return {};
    if (!json["offsets"].is_object())
        return Malformed("'offsets' is not an object");

    for (const auto& [key, entry] : json["offsets"].items())
    {
        if (Status claimed = ClaimKey(owners, key, "offsets"); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("offsets.{} is not an object", key));

        if (SkipOtherPlatform(entry, platform, key, out))
            continue;

        OffsetEntry offset;
        offset.Max = entry.value("max", MaxByteOffset);
        offset.Align = entry.value("align", 1);
        if (offset.Align <= 0)
            return Malformed(std::format("offsets.{}.align must be positive (got {})", key, offset.Align));

        auto value = PlatformInt(entry, platform, "offsets", key);
        if (!value)
            return std::unexpected(value.error());
        if (!IsOffsetInRange(*value, offset.Max))
            return Malformed(std::format("offsets.{} value {} is outside [0, {}]", key, *value, offset.Max));
        if (!IsAlignedOffset(*value, offset.Align))
            return Malformed(std::format("offsets.{} value {} is not aligned to {}", key, *value, offset.Align));

        offset.Value = *value;
        out.Offsets.emplace(key, offset);
    }
    return {};
}

static Status ParseMessages(const nlohmann::json& json, GamePlatform platform, GameDataFile& out,
                            std::map<std::string, std::string>& owners)
{
    if (!json.contains("messages"))
        return {};
    if (!json["messages"].is_object())
        return Malformed("'messages' is not an object");

    for (const auto& [key, entry] : json["messages"].items())
    {
        if (Status claimed = ClaimKey(owners, key, "messages"); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("messages.{} is not an object", key));

        if (SkipOtherPlatform(entry, platform, key, out))
            continue;

        auto value = PlatformInt(entry, platform, "messages", key);
        if (!value)
            return std::unexpected(value.error());
        if (*value < 0)
            return Malformed(std::format("messages.{} value {} is negative", key, *value));

        out.Messages.emplace(key, *value);
    }
    return {};
}

static Result<GameDataFile> ParseChecked(std::string_view text, GamePlatform platform)
{
    nlohmann::json json;
    try
    {
        json = nlohmann::json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    }
    catch (const std::exception& e)
    {
        return Malformed(std::format("not valid JSON: {}", e.what()));
    }

    if (!json.is_object())
        return Malformed("the document root is not an object");

    if (!json.contains("version") || !json["version"].is_number_integer())
        return Malformed("no integer 'version'");
    const int version = json["version"].get<int>();
    if (version != GameDataFormatVersion)
        return Malformed(std::format("unsupported version {} (this build reads {})", version, GameDataFormatVersion));

    GameDataFile out;
    out.Version = version;
    if (json.contains("build"))
    {
        const auto& build = json["build"];
        if (!build.is_object())
            return Malformed("'build' is not an object");
        out.Build.Game = build.value("game", std::string{});
        out.Build.Verified = build.value("verified", std::string{});
        out.Build.Note = build.value("note", std::string{});
    }

    // Keys are the names Bindings resolves by, so one key may only mean one thing.
    std::map<std::string, std::string> owners;

    // Signatures first: `addresses` entries are checked against them as they parse.
    if (Status parsed = ParseSignatures(json, platform, out, owners); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseAddresses(json, platform, out, owners); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseVTables(json, platform, out, owners); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseMessages(json, platform, out, owners); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseOffsets(json, platform, out, owners); !parsed)
        return std::unexpected(parsed.error());

    return out;
}

Result<GameDataFile> GameDataFile::Parse(std::string_view text, GamePlatform platform)
{
    // The checks above name the offending path for every shape seen in practice. This is the
    // backstop for the rest: nlohmann throws type_error on a conversion, and Parse is called
    // from Runtime::Start, where an escaping exception would take the load down instead of
    // degrading it.
    try
    {
        return ParseChecked(text, platform);
    }
    catch (const std::exception& e)
    {
        return Malformed(std::format("malformed structure: {}", e.what()));
    }
}

Result<GameDataFile> GameDataFile::Load(std::string_view path, GamePlatform platform)
{
    auto resolved = ResolvePath(path);
    std::ifstream file(resolved);
    if (!file.is_open())
        return std::unexpected(Error::NotFound(std::format("gamedata file not found: {}", resolved.string())));

    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = Parse(text, platform);
    if (!parsed)
        return std::unexpected(Error::Invalid(std::format("{}: {}", path, parsed.error().Detail)));
    return parsed;
}

}  // namespace VoltMod
