#pragma once

#include "Host/Plugins/CallbackList.hpp"

#include <VoltMod/Host/IHostServices.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The process-wide table of interfaces plugins publish to each other.
 *
 * An owner is whatever opaque pointer the host identifies a publisher by; only that pointer can
 * refresh or withdraw what it published. Entries stay in publish order, which is the replay order.
 */
class ServiceTable
{
public:
    using OwnerId = const void*;

    /** Add or refresh @p name. A peer's live pointer is never swapped out from under it. */
    void Publish(OwnerId owner, std::string_view name, void* implementation);
    void Unpublish(OwnerId owner, std::string_view name);
    void* Find(std::string_view name) const;

    /** Withdraw everything @p owner published and report the names it was still holding. */
    std::vector<std::string> RemoveAll(OwnerId owner);

    /** Hand @p callback what is already published: what a late subscriber is promised. */
    void NotifyPublished(IHostServices::ChangedFn callback, void* context) const;

    CallbackList<IHostServices::ChangedFn>& Changed() { return _changed; }

private:
    struct Service
    {
        std::string Name;
        void* Implementation = nullptr;
        OwnerId Owner = nullptr;
    };

    void RaiseChanged(std::string_view name, bool published);

    std::vector<Service> _services;
    CallbackList<IHostServices::ChangedFn> _changed;
};

}  // namespace VoltMod
