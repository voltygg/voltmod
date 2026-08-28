#include "Ui/ClickPayload.hpp"

#include <format>
#include <utility>

namespace VoltMod
{

/** Protobuf wire types; only these three can appear ahead of the fields we want. */
static constexpr uint32_t kWireVarint = 0;
static constexpr uint32_t kWireFixed64 = 1;
static constexpr uint32_t kWireLengthDelimited = 2;
static constexpr uint32_t kWireFixed32 = 5;

/** A varint is at most ten bytes; more than that is malformed rather than merely large. */
static constexpr int kMaxVarintBytes = 10;

/** Read a base-128 varint at @p at, advancing it past what it consumed. */
static Result<uint64_t> ReadVarint(std::string_view bytes, size_t& at)
{
    uint64_t value = 0;
    for (int byte = 0; byte < kMaxVarintBytes; ++byte)
    {
        if (at >= bytes.size())
            return std::unexpected(Error::Invalid("a varint runs past the end of the payload"));

        const auto part = static_cast<uint8_t>(bytes[at++]);
        value |= static_cast<uint64_t>(part & 0x7Fu) << (7 * byte);
        if ((part & 0x80u) == 0)
            return value;
    }
    return std::unexpected(Error::Invalid("a varint is longer than ten bytes"));
}

/** Step @p at past a field of @p wireType whose key has already been read. */
static Status SkipField(std::string_view bytes, size_t& at, uint32_t wireType)
{
    switch (wireType)
    {
    case kWireVarint:
    {
        auto skipped = ReadVarint(bytes, at);
        return skipped ? Status{} : std::unexpected(skipped.error());
    }
    case kWireFixed64:
        at += 8;
        break;
    case kWireFixed32:
        at += 4;
        break;
    case kWireLengthDelimited:
    {
        auto length = ReadVarint(bytes, at);
        if (!length)
            return std::unexpected(length.error());

        // Compared against the bytes left rather than added first: a length near 2^64 would wrap
        // the cursor round and land it back inside the payload.
        if (*length > bytes.size() - at)
            return std::unexpected(Error::Invalid("a field runs past the end of the payload"));

        at += static_cast<size_t>(*length);
        break;
    }
    default:
        // Groups (3 and 4) are gone from proto3 and nothing else is legal, so a length cannot
        // be guessed and the rest of the payload is unreadable.
        return std::unexpected(Error::Invalid(std::format("unknown protobuf wire type {}", wireType)));
    }

    if (at > bytes.size())
        return std::unexpected(Error::Invalid("a field runs past the end of the payload"));
    return {};
}

Result<ClickPayload> ParseClickPayload(std::string_view bytes)
{
    ClickPayload out;
    bool haveLayout = false;
    bool haveButton = false;

    size_t at = 0;
    while (at < bytes.size())
    {
        auto key = ReadVarint(bytes, at);
        if (!key)
            return std::unexpected(key.error());

        const auto field = static_cast<uint32_t>(*key >> 3);
        const auto wireType = static_cast<uint32_t>(*key & 0x7u);

        if (field == 1 && wireType == kWireVarint)
        {
            auto layout = ReadVarint(bytes, at);
            if (!layout)
                return std::unexpected(layout.error());

            out.Layout = static_cast<uint32_t>(*layout);
            haveLayout = true;
            continue;
        }

        if (field == 2 && wireType == kWireLengthDelimited)
        {
            auto length = ReadVarint(bytes, at);
            if (!length)
                return std::unexpected(length.error());
            if (*length > bytes.size() - at)
                return std::unexpected(Error::Invalid("the button id runs past the end of the payload"));

            out.Button.assign(bytes, at, static_cast<size_t>(*length));
            at += static_cast<size_t>(*length);
            haveButton = true;
            continue;
        }

        if (Status skipped = SkipField(bytes, at, wireType); !skipped)
            return std::unexpected(skipped.error());
    }

    if (!haveLayout || !haveButton)
        return std::unexpected(Error::Invalid("the payload carries no layout handle or no button id"));

    return out;
}

}  // namespace VoltMod
