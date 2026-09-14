#pragma once

#include <VoltMod/Core/Result.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The two fields a custom HUD Button press carries, read out of a user message's `msg_data`.
 *
 * Hand-parsed: the body is not a registered message and the SDK's protos do not declare it, so
 * there is nothing to reflect on. Unknown fields are skipped, so a field CS2 adds later does not
 * stop these two from being read. SDK-free for its tests.
 */
class ButtonPressMessage
{
public:
    /** Error::Invalid when the bytes are not well-formed protobuf, a length runs past the end, or
     *  either field is missing or carries the wrong wire type. */
    static Result<ButtonPressMessage> Parse(std::string_view bytes);

    uint32_t LayoutHandle = 0;  ///< field 1, varint: the custom_hud_layout's EHANDLE
    std::string ButtonId;       ///< field 2, length-delimited: the Button's `id` attribute

private:
    static Result<uint64_t> ReadVarint(std::string_view bytes, size_t& at);
    static Status SkipField(std::string_view bytes, size_t& at, uint32_t wireType);
};

}  // namespace VoltMod
