#pragma once

#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <functional>

namespace VoltMod
{

/**
 * @brief The eight engine hooks, installed once for the whole process.
 *
 * Each one raises the matching event on @ref PluginHost, which calls every loaded plugin in load
 * order. Game thread only, like everything downstream of it.
 */
class EngineHooks
{
public:
    /**
     * @param beforeFrame Runs at the start of every frame, before the frame reaches the plugins.
     * @param beforeServerStartup Runs on every map change, before the plugins hear about it.
     */
    EngineHooks(PluginHost& host, std::function<void()> beforeFrame, std::function<void()> beforeServerStartup);
    ~EngineHooks();

    EngineHooks(const EngineHooks&) = delete;
    EngineHooks& operator=(const EngineHooks&) = delete;

    /** Resolve the engine interfaces the hooks need from @p metamod, then install them. */
    Status Install(SourceMM::ISmmAPI* metamod);

    /** Remove every hook. Nothing reaches the plugins after this returns. */
    void Uninstall();

private:
    PluginHost& _host;
    std::function<void()> _beforeFrame;
    std::function<void()> _beforeServerStartup;
    Subscriptions _hooks;
};

}  // namespace VoltMod
