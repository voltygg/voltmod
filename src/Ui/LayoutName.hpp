#pragma once

#include <VoltMod/Core/Result.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/** The only directory gameinfo.gi's addon whitelist allows Panorama layouts in. */
inline constexpr std::string_view kLayoutRoot = "panorama/layout/custom_game/";

/**
 * Expand a bare layout name, and refuse one the client could only reject silently.
 *
 * `welcome` and `welcome.xml` both become `panorama/layout/custom_game/welcome.xml`; a full path
 * must already be under @ref kLayoutRoot and name the source `.xml`.
 *
 * Refusing server-side is the point: an unmounted or misspelled resource renders nothing and says
 * so only on the *client* console, which is the hardest failure here to diagnose. SDK-free so the
 * rules are unit-tested.
 */
Result<std::string> ResolveLayoutName(std::string_view layout);

}  // namespace VoltMod
