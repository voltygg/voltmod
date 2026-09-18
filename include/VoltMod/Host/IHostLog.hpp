#pragma once

#include <VoltMod/Host/HostTypes.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Where one plugin's log lines go.
 *
 * The host owns the console and prints every plugin's lines through the same sink, so a server
 * sees one stream rather than one per loaded plugin. Each plugin has its own view of this, which
 * is how the host knows whose line it is printing.
 *
 * Formatting and the queue for lines raised on a worker thread stay in the SDK: this is only the
 * output, and it is called on the game thread.
 */
struct IHostLog
{
    static constexpr const char* InterfaceName = "VoltMod.IHostLog";

    /** Name this plugin's lines. Called once at load; the host prefixes it to every line. */
    virtual void SetTag(HostString tag) = 0;

    /** Print one line. @p level is a @ref LogLevel. */
    virtual void Write(uint8_t level, HostString text) = 0;

    /** Lines below this are dropped. The SDK reads it once a frame and skips formatting them,
     *  so a silenced plugin costs nothing. `volt log <name> <level>` is what changes it. */
    virtual uint8_t MinLevel() = 0;

protected:
    ~IHostLog() = default;
};

}  // namespace VoltMod
