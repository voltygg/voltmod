#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief The one comparison of the baked schema offsets against the live game, per process.
 *
 * The offsets are compiled into each plugin's own copy of the SDK, so the host's answer only
 * covers a plugin built from the same generated layout. @ref LayoutStamp is how a plugin tells:
 * equal stamps mean the same offsets and the host's answer stands, and a different stamp means
 * the plugin was built against another layout and has to be rebuilt.
 */
struct IHostSchema
{
    static constexpr const char* InterfaceName = "VoltMod.IHostSchema";

    /** The hash of the layout the host verified. */
    virtual uint64_t LayoutStamp() const = 0;

    /** Whether that layout matched the live schema. The host logs every mismatch itself. */
    virtual bool Verified() const = 0;

protected:
    ~IHostSchema() = default;
};

}  // namespace VoltMod
