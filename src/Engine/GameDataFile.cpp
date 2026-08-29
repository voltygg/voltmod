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
 * What a section reader writes to: the platform column being kept, the file being built, and the
 * section each key was first claimed by, so a collision can say which two sections clash.
 */
struct SectionContext
{
    GamePlatform Platform;
    GameDataFile& Out;
    std::map<std::string, std::string> Owners;
};

/** Keys are the names Bindings resolves by, so one key may only mean one thing. */
static Status ClaimKey(SectionContext& ctx, const std::string& key, std::string_view section)
{
    auto [it, inserted] = ctx.Owners.emplace(key, section);
    if (!inserted)
        return Malformed(std::format("'{}' is declared in both '{}' and '{}'", key, it->second, section));
    return {};
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
static bool SkipOtherPlatform(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    if (!entry.is_object() || entry.contains(PlatformKey(ctx.Platform)) || !HasOtherPlatform(entry, ctx.Platform))
        return false;

    ctx.Out.OtherPlatformOnly.push_back(key);
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

/**
 * Reads one entry that has already passed the checks every section shares, into @p ctx. Storing
 * nothing is how a reader accepts an entry that does not apply to this platform.
 */
using SectionReader = Status (*)(SectionContext& ctx, const std::string& key, const nlohmann::json& entry);

static Status ReadSignature(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    const std::string_view column = PlatformKey(ctx.Platform);
    if (!entry.contains(column))
        return Malformed(std::format("signatures.{} has no '{}' entry", key, column));
    if (!entry[column].is_object())
        return Malformed(std::format("signatures.{}.{} is not an object", key, column));

    SignatureEntry signature;
    signature.Library = entry.value("library", std::string("server"));
    signature.Pattern = entry[column].value("pattern", std::string{});
    if (!IsValidBytePattern(signature.Pattern))
        return Malformed(std::format("signatures.{}.{}.pattern is not a byte pattern", key, column));

    ctx.Out.Signatures.emplace(key, std::move(signature));
    return {};
}

static Status ReadAddress(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    AddressEntry address;
    address.Signature = entry.value("signature", std::string{});
    // An address derived from a signature this platform does not carry goes with it, rather
    // than reading as a reference to a signature nobody wrote.
    if (std::ranges::contains(ctx.Out.OtherPlatformOnly, address.Signature))
    {
        ctx.Out.OtherPlatformOnly.push_back(key);
        return {};
    }
    if (!ctx.Out.Signatures.contains(address.Signature))
        return Malformed(std::format("addresses.{} derives from unknown signature '{}'", key, address.Signature));

    if (!entry.contains("rel32At"))
        return Malformed(std::format("addresses.{} has no 'rel32At'", key));

    // The platform columns are on `rel32At`, not on the entry: the shared check above saw neither.
    if (SkipOtherPlatform(ctx, key, entry["rel32At"]))
        return {};

    auto rel32At = PlatformInt(entry["rel32At"], ctx.Platform, "addresses", key + ".rel32At");
    if (!rel32At)
        return std::unexpected(rel32At.error());
    if (*rel32At < 0)
        return Malformed(std::format("addresses.{}.rel32At is negative ({})", key, *rel32At));

    address.Rel32At = *rel32At;
    ctx.Out.Addresses.emplace(key, std::move(address));
    return {};
}

static Status ReadVTable(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    VTableEntry vtable;
    vtable.Class = entry.value("class", std::string{});
    if (vtable.Class.empty())
        return Malformed(std::format("vtables.{} has no 'class'", key));
    vtable.Library = entry.value("library", std::string("server"));

    auto index = PlatformInt(entry, ctx.Platform, "vtables", key);
    if (!index)
        return std::unexpected(index.error());
    if (*index < 0 || *index >= MaxVtableIndex)
        return Malformed(std::format("vtables.{} index {} is outside [0, {})", key, *index, MaxVtableIndex));

    vtable.Index = *index;
    ctx.Out.VTables.emplace(key, std::move(vtable));
    return {};
}

static Status ReadMessage(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    auto value = PlatformInt(entry, ctx.Platform, "messages", key);
    if (!value)
        return std::unexpected(value.error());
    if (*value < 0)
        return Malformed(std::format("messages.{} value {} is negative", key, *value));

    ctx.Out.Messages.emplace(key, *value);
    return {};
}

static Status ReadOffset(SectionContext& ctx, const std::string& key, const nlohmann::json& entry)
{
    OffsetEntry offset;
    offset.Max = entry.value("max", MaxByteOffset);
    offset.Align = entry.value("align", 1);
    if (offset.Align <= 0)
        return Malformed(std::format("offsets.{}.align must be positive (got {})", key, offset.Align));

    auto value = PlatformInt(entry, ctx.Platform, "offsets", key);
    if (!value)
        return std::unexpected(value.error());
    if (!IsOffsetInRange(*value, offset.Max))
        return Malformed(std::format("offsets.{} value {} is outside [0, {}]", key, *value, offset.Max));
    if (!IsAlignedOffset(*value, offset.Align))
        return Malformed(std::format("offsets.{} value {} is not aligned to {}", key, *value, offset.Align));

    offset.Value = *value;
    ctx.Out.Offsets.emplace(key, offset);
    return {};
}

/**
 * The shape every section shares: an optional object of named entries, each key claimed exactly
 * once across the whole file, each entry an object, and each entry either this platform's or
 * recorded as the other platform's. @p read sees only what got past all four.
 */
static Status ParseSection(const nlohmann::json& json, SectionContext& ctx, std::string_view section,
                           SectionReader read)
{
    const auto found = json.find(section);
    if (found == json.end())
        return {};
    if (!found->is_object())
        return Malformed(std::format("'{}' is not an object", section));

    for (const auto& [key, entry] : found->items())
    {
        if (Status claimed = ClaimKey(ctx, key, section); !claimed)
            return claimed;
        if (!entry.is_object())
            return Malformed(std::format("{}.{} is not an object", section, key));
        if (SkipOtherPlatform(ctx, key, entry))
            continue;
        if (Status stored = read(ctx, key, entry); !stored)
            return stored;
    }
    return {};
}

// Signatures first: `addresses` entries are checked against them as they parse.
static constexpr std::pair<std::string_view, SectionReader> GameDataSections[] = {
    {"signatures", ReadSignature}, {"addresses", ReadAddress}, {"vtables", ReadVTable},
    {"messages", ReadMessage},     {"offsets", ReadOffset},
};

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

    SectionContext ctx{.Platform = platform, .Out = out};
    for (const auto& [section, read] : GameDataSections)
    {
        if (Status parsed = ParseSection(json, ctx, section, read); !parsed)
            return std::unexpected(parsed.error());
    }

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
