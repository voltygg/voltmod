#pragma once

#include <VoltMod/Host/HostTypes.hpp>

namespace VoltMod
{

/**
 * @brief The process-wide table of interfaces plugins publish to each other.
 *
 * A published pointer is valid only while its publisher is loaded, so look it up at the point of
 * use. The host never loads or unloads a plugin inside a fan-out, which is what makes a pointer
 * fetched and used inside one callback safe.
 */
struct IHostServices
{
    static constexpr const char* InterfaceName = "VoltMod.IHostServices";

    /** @p published is false when the name was withdrawn. */
    using ChangedFn = void (*)(void* context, HostString name, bool published);

    virtual void Publish(HostString name, void* implementation) = 0;
    virtual void Unpublish(HostString name) = 0;
    virtual void* Find(HostString name) = 0;

    /** Raised for every plugin's publications, including those already in the table when the
     *  subscription is made. */
    virtual HostToken SubscribeChanged(ChangedFn call, void* context) = 0;
    virtual void Unsubscribe(HostToken token) = 0;

protected:
    ~IHostServices() = default;
};

}  // namespace VoltMod
