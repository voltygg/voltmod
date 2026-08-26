#pragma once

#include <VoltMod/App/PluginManifest.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief Holds this plugin's manifest, announces it to peers, reports unmet dependencies.
 *
 * Owned by the runtime, so the published pointer lives exactly one load cycle: the destructor
 * withdraws it while the ServiceExchange it was published in is still alive.
 *
 * The dependency report is advisory and runs on the first game frame, not at OnLoad: .vdf
 * load order is Metamod's, so during load a peer that comes later looks exactly like one
 * that is missing. Callers still handle a null from ServiceExchange::Get, which stays
 * correct whenever a peer arrives or leaves.
 */
class PluginIdentity final : public IPluginIdentity
{
public:
    /** @p exchange and @p scheduler must outlive this object; both are declared above it. */
    PluginIdentity(ServiceExchange& exchange, Scheduler& scheduler) : _exchange(exchange), _scheduler(scheduler) {}
    ~PluginIdentity() { Withdraw(); }
    PluginIdentity(const PluginIdentity&) = delete;
    PluginIdentity& operator=(const PluginIdentity&) = delete;

    /** Take @p manifest, publish it, and queue the dependency report. */
    void Adopt(PluginManifest manifest);
    /** Stop answering peer lookups. Idempotent; the destructor calls it. */
    void Withdraw();

    /** What this plugin declared; empty when it ships no manifest. */
    const PluginManifest& Manifest() const { return _manifest; }

    const char* PluginVersion() const override { return _manifest.Version.c_str(); }

private:
    ServiceExchange& _exchange;
    Scheduler& _scheduler;
    PluginManifest _manifest;
    std::string _key;
};

}  // namespace VoltMod
