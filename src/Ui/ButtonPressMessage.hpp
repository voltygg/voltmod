#pragma once

#include <VoltMod/Core/Results/Result.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The button id a custom HUD Button press carries, read out of a user message's `msg_data`.
 *
 * Hand-parsed: the body is not a registered message and the SDK's protos do not declare it, so
 * there is nothing to reflect on. Every other field is skipped, so a field CS2 adds later does not
 * stop the id from being read. SDK-free for its tests.
 */
struct ButtonPressMessage
{
    /** Error::Invalid when the bytes are not well-formed protobuf, a length runs past the end, or the
     *  button id is missing or carries the wrong wire type. */
    static Result<ButtonPressMessage> Parse(std::string_view bytes);

    std::string ButtonId;  ///< field 2, length-delimited: the Button's `id` attribute
};

}  // namespace VoltMod
