#include "Engine/GameDataFile.hpp"

#include "Engine/GameDataDocument.hpp"

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Engine/OffsetCheck.hpp>
#include <algorithm>
#include <cctype>
#include <format>
#include <map>
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

/** @p platform's column on @p entry. Every columned entry type spells the pair the same way, so
 *  this is the one place the platform-to-member mapping lives. */
template <class TEntry>
static const auto& ColumnOf(const TEntry& entry, GamePlatform platform)
{
    return platform == GamePlatform::Windows ? entry.Windows : entry.Linux;
}

/** Whether @p entry carries @p platform's column. An entry type with no columns of its own
 *  (`addresses`, whose columns live on `rel32At`) never has one. */
template <class TEntry>
static bool HasColumn(const TEntry& entry, GamePlatform platform)
{
    if constexpr (requires { entry.Windows; })
        return ColumnOf(entry, platform).has_value();
    else
        return false;
}

/** The diagnostic every column reader shares when @p key has no column for @p platform. */
static std::unexpected<Error> NoColumn(std::string_view section, const std::string& key, GamePlatform platform)
{
    return Malformed(std::format("{}.{} has no '{}' entry", section, key, PlatformKey(platform)));
}

/**
 * Record @p key as unavailable here and tell the caller to skip it.
 *
 * gamedata.schema.json requires one of the two columns, not both: something only located on
 * Windows so far is a capability that is off on Linux, not a malformed entry.
 */
template <class TEntry>
static bool SkipOtherPlatform(SectionContext& ctx, const std::string& key, const TEntry& entry)
{
    const GamePlatform other = ctx.Platform == GamePlatform::Windows ? GamePlatform::Linux : GamePlatform::Windows;
    if (HasColumn(entry, ctx.Platform) || !HasColumn(entry, other))
        return false;

    ctx.Out.OtherPlatformOnly.push_back(key);
    return true;
}

/** Read one platform column of @p entry, or report which key has no column. The value is already
 *  an integer: a non-integer was rejected while parsing. */
template <class TEntry>
static Result<int> PlatformColumn(const TEntry& entry, GamePlatform platform, std::string_view section,
                                  const std::string& key)
{
    const auto& column = ColumnOf(entry, platform);
    if (!column.has_value())
        return NoColumn(section, key, platform);
    return *column;
}

static Status ReadSignature(SectionContext& ctx, const std::string& key, const GameDataDocument::Signature& entry)
{
    const std::string_view column = PlatformKey(ctx.Platform);
    const auto& pattern = ColumnOf(entry, ctx.Platform);
    if (!pattern.has_value())
        return NoColumn("signatures", key, ctx.Platform);

    SignatureEntry signature;
    signature.Library = entry.library;
    signature.Pattern = pattern->pattern;
    if (!IsValidBytePattern(signature.Pattern))
        return Malformed(std::format("signatures.{}.{}.pattern is not a byte pattern", key, column));

    ctx.Out.Signatures.emplace(key, std::move(signature));
    return {};
}

static Status ReadAddress(SectionContext& ctx, const std::string& key, const GameDataDocument::Address& entry)
{
    AddressEntry address;
    address.Signature = entry.signature;
    // An address derived from a signature this platform does not carry goes with it, rather
    // than reading as a reference to a signature nobody wrote.
    if (std::ranges::contains(ctx.Out.OtherPlatformOnly, address.Signature))
    {
        ctx.Out.OtherPlatformOnly.push_back(key);
        return {};
    }
    if (!ctx.Out.Signatures.contains(address.Signature))
        return Malformed(std::format("addresses.{} derives from unknown signature '{}'", key, address.Signature));

    if (!entry.rel32At.has_value())
        return Malformed(std::format("addresses.{} has no 'rel32At'", key));

    // The platform columns are on `rel32At`, not on the entry: the shared check above saw neither.
    if (SkipOtherPlatform(ctx, key, *entry.rel32At))
        return {};

    auto rel32At = PlatformColumn(*entry.rel32At, ctx.Platform, "addresses", key + ".rel32At");
    if (!rel32At)
        return std::unexpected(rel32At.error());
    if (*rel32At < 0)
        return Malformed(std::format("addresses.{}.rel32At is negative ({})", key, *rel32At));

    address.Rel32At = *rel32At;
    ctx.Out.Addresses.emplace(key, std::move(address));
    return {};
}

static Status ReadVTable(SectionContext& ctx, const std::string& key, const GameDataDocument::VTable& entry)
{
    VTableEntry vtable;
    vtable.Class = entry.Class;
    if (vtable.Class.empty())
        return Malformed(std::format("vtables.{} has no 'class'", key));
    vtable.Library = entry.library;

    auto index = PlatformColumn(entry, ctx.Platform, "vtables", key);
    if (!index)
        return std::unexpected(index.error());
    if (*index < 0 || *index >= MaxVtableIndex)
        return Malformed(std::format("vtables.{} index {} is outside [0, {})", key, *index, MaxVtableIndex));

    vtable.Index = *index;
    ctx.Out.VTables.emplace(key, std::move(vtable));
    return {};
}

static Status ReadMessage(SectionContext& ctx, const std::string& key, const GameDataDocument::Columns& entry)
{
    auto value = PlatformColumn(entry, ctx.Platform, "messages", key);
    if (!value)
        return std::unexpected(value.error());
    if (*value < 0)
        return Malformed(std::format("messages.{} value {} is negative", key, *value));

    ctx.Out.Messages.emplace(key, *value);
    return {};
}

static Status ReadOffset(SectionContext& ctx, const std::string& key, const GameDataDocument::Offset& entry)
{
    OffsetEntry offset;
    offset.Max = entry.max;
    offset.Align = entry.align;
    if (offset.Align <= 0)
        return Malformed(std::format("offsets.{}.align must be positive (got {})", key, offset.Align));

    auto value = PlatformColumn(entry, ctx.Platform, "offsets", key);
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
 * The shape every section shares: each key claimed exactly once across the whole file, and each
 * entry either this platform's or recorded as the other platform's. @p read sees only what got
 * past both. That an entry is an object of the right shape was settled while parsing.
 */
template <class TEntry>
static Status ParseSection(const std::map<std::string, TEntry>& entries, SectionContext& ctx, std::string_view section,
                           Status (*read)(SectionContext&, const std::string&, const TEntry&))
{
    for (const auto& [key, entry] : entries)
    {
        if (Status claimed = ClaimKey(ctx, key, section); !claimed)
            return claimed;
        if (SkipOtherPlatform(ctx, key, entry))
            continue;
        if (Status stored = read(ctx, key, entry); !stored)
            return stored;
    }
    return {};
}

Result<GameDataFile> GameDataFile::Parse(std::string_view text, GamePlatform platform)
{
    // Every shape error is reported as a value, so nothing can throw out of here into
    // Runtime::Start - the exception backstop this function used to carry is gone. An unknown key
    // is now rejected too, which is gamedata.schema.json's additionalProperties being enforced.
    auto document = Json::Read<GameDataDocument, Json::StrictReadOptions>(text);
    if (!document)
        return Malformed(std::format("not valid JSON: {}", document.error().Detail));

    if (!document->version.has_value())
        return Malformed("no integer 'version'");
    if (*document->version != GameDataFormatVersion)
        return Malformed(
            std::format("unsupported version {} (this build reads {})", *document->version, GameDataFormatVersion));

    GameDataFile out;
    out.Version = *document->version;
    out.Build.Game = document->build.game;
    out.Build.Verified = document->build.verified;
    out.Build.Note = document->build.note;

    SectionContext ctx{.Platform = platform, .Out = out};
    // Signatures first: `addresses` entries are checked against them as they parse.
    if (Status parsed = ParseSection(document->signatures, ctx, "signatures", ReadSignature); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseSection(document->addresses, ctx, "addresses", ReadAddress); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseSection(document->vtables, ctx, "vtables", ReadVTable); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseSection(document->messages, ctx, "messages", ReadMessage); !parsed)
        return std::unexpected(parsed.error());
    if (Status parsed = ParseSection(document->offsets, ctx, "offsets", ReadOffset); !parsed)
        return std::unexpected(parsed.error());

    return out;
}

Result<GameDataFile> GameDataFile::Load(std::string_view path, GamePlatform platform)
{
    auto text = ReadAllText(path);
    if (!text)
        return std::unexpected(text.error());

    auto parsed = Parse(*text, platform);
    if (!parsed)
        return std::unexpected(Error::Invalid(std::format("{}: {}", path, parsed.error().Detail)));
    return parsed;
}

}  // namespace VoltMod
