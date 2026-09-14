#include "Ui/ButtonPressMessage.hpp"

#include <cstddef>
#include <format>
#include <utility>

namespace VoltMod
{

/** Protobuf wire types; only these can appear ahead of the fields we want. */
static constexpr uint32_t WireVarint = 0;
static constexpr uint32_t WireFixed64 = 1;
static constexpr uint32_t WireLengthDelimited = 2;
static constexpr uint32_t WireFixed32 = 5;

/** A varint is at most ten bytes; more is malformed rather than merely large. */
static constexpr int MaxVarintBytes = 10;

static Result<uint64_t> ReadVarint(std::string_view bytes, size_t& at)
{
    uint64_t value = 0;
    for (int byte = 0; byte < MaxVarintBytes; ++byte)
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

static Status SkipField(std::string_view bytes, size_t& at, uint32_t wireType)
{
    switch (wireType)
    {
    case WireVarint:
    {
        auto skipped = ReadVarint(bytes, at);
        return skipped ? Status{} : std::unexpected(skipped.error());
    }
    case WireFixed64:
        at += 8;
        break;
    case WireFixed32:
        at += 4;
        break;
    case WireLengthDelimited:
    {
        auto length = ReadVarint(bytes, at);
        if (!length)
            return std::unexpected(length.error());

        // Compared against the bytes left, not added first: a length near 2^64 would wrap the cursor.
        if (*length > bytes.size() - at)
            return std::unexpected(Error::Invalid("a field runs past the end of the payload"));

        at += static_cast<size_t>(*length);
        break;
    }
    default:
        // Groups are gone from proto3 and nothing else is legal, so the rest cannot be found.
        return std::unexpected(Error::Invalid(std::format("unknown protobuf wire type {}", wireType)));
    }

    if (at > bytes.size())
        return std::unexpected(Error::Invalid("a field runs past the end of the payload"));
    return {};
}

Result<ButtonPressMessage> ButtonPressMessage::Parse(std::string_view bytes)
{
    ButtonPressMessage out;
    bool haveButton = false;

    size_t at = 0;
    while (at < bytes.size())
    {
        auto key = ReadVarint(bytes, at);
        if (!key)
            return std::unexpected(key.error());

        const auto field = static_cast<uint32_t>(*key >> 3);
        const auto wireType = static_cast<uint32_t>(*key & 0x7u);

        if (field == 2 && wireType == WireLengthDelimited)
        {
            auto length = ReadVarint(bytes, at);
            if (!length)
                return std::unexpected(length.error());
            if (*length > bytes.size() - at)
                return std::unexpected(Error::Invalid("the button id runs past the end of the payload"));

            out.ButtonId.assign(bytes, at, static_cast<size_t>(*length));
            at += static_cast<size_t>(*length);
            haveButton = true;
            continue;
        }

        if (Status skipped = SkipField(bytes, at, wireType); !skipped)
            return std::unexpected(skipped.error());
    }

    if (!haveButton)
        return std::unexpected(Error::Invalid("the payload carries no button id"));

    return out;
}

}  // namespace VoltMod
