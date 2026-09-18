#pragma once

#include <cstdint>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The process-wide table of interfaces plugins publish to each other.
 *
 * A published pointer lives as long as its publisher stays loaded, so look it up where it is used.
 * The host never unloads a plugin inside a dispatch.
 */
struct IHostServices
{
    /** @p published is false when the name was withdrawn. */
    using ChangedFn = void (*)(void* context, std::string_view name, bool published);

    virtual void Publish(std::string_view name, void* implementation) = 0;
    virtual void Unpublish(std::string_view name) = 0;
    virtual void* Find(std::string_view name) = 0;

    /** Also replays what is already published when the subscription is made. */
    virtual uint64_t OnChanged(ChangedFn callback, void* context) = 0;
    virtual void Unsubscribe(uint64_t token) = 0;

protected:
    ~IHostServices() = default;
};

}  // namespace VoltMod
