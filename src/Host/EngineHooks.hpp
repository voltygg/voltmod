#pragma once

#include "Host/PluginHost.hpp"

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
     *        The plugin loader acts on what `volt` queued there, so a library is never loaded or
     *        freed inside a dispatch.
     */
    EngineHooks(PluginHost& host, std::function<void()> beforeFrame);
    ~EngineHooks();

    EngineHooks(const EngineHooks&) = delete;
    EngineHooks& operator=(const EngineHooks&) = delete;

    /** Resolve the engine interfaces the hooks need from @p metamod, then install them. */
    Status Start(SourceMM::ISmmAPI* metamod);

    /** Remove every hook. Nothing reaches the plugins after this returns. */
    void Stop();

private:
    PluginHost& _host;
    std::function<void()> _beforeFrame;
    Subscriptions _hooks;
};

}  // namespace VoltMod
