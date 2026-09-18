#pragma once

#include <cstddef>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief A borrowed string crossing the host boundary.
 *
 * Valid for the duration of the call only. A receiver that keeps the text copies it: the host
 * and each plugin have their own allocator, so no owned type may cross.
 */
struct HostString
{
    const char* Data = nullptr;
    size_t Length = 0;
};

/** Identifies one subscription for removal. Never zero while it is live. */
using HostToken = uint64_t;

}  // namespace VoltMod
