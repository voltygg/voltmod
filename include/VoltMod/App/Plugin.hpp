#pragma once

#include <VoltMod/Runtime.hpp>

namespace VoltMod
{

/**
 * @brief Everything a plugin owns for one load cycle.
 *
 * The framework constructs the derived class with a live @ref Runtime, calls @ref Load, and
 * destroys it before that runtime. Release plugin-owned resources in member destructors.
 */
class Plugin
{
public:
    explicit Plugin(VoltMod::Runtime& runtime) : Runtime(runtime) {}
    virtual ~Plugin() = default;

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    /** Framework services for this plugin's load cycle. */
    VoltMod::Runtime& Runtime;

    /** Start work that can fail the load, after every member is built. Returning false aborts it. */
    virtual bool Load() { return true; }
};

}  // namespace VoltMod
