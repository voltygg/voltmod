#pragma once

#include <VoltMod/Core/Result.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/** The two fields a custom HUD Button press carries. */
struct ClickPayload
{
    uint32_t Layout = 0;  ///< field 1, varint: the custom_hud_layout's EHANDLE.
    std::string Button;   ///< field 2, length-delimited: the Button's `id` attribute.
};

/**
 * Read a custom HUD press out of a user message's `msg_data`.
 *
 * Hand-parsed rather than reflected because this body has no descriptor to reflect on: it is not a
 * registered message and the SDK's protos do not declare it, so there is no generated type and
 * nothing for `Message::ToPB` to produce. Two fields of protobuf is a small enough grammar to read
 * directly, and doing it here keeps it SDK-free and testable.
 *
 * Unknown fields are skipped rather than refused, so a field CS2 adds later does not stop the two
 * this needs from being read.
 *
 * @return Error::Invalid when the bytes are not well-formed protobuf, a length runs past the end,
 *         or either field is missing or carries the wrong wire type.
 */
Result<ClickPayload> ParseClickPayload(std::string_view bytes);

}  // namespace VoltMod
