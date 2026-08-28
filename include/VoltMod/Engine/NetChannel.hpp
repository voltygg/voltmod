#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Per-client network-channel access: latency and the client's replicated userinfo cvars.
 *
 * Stateless wrapper over IVEngineServer2: every call reads the engine live, so nothing here needs a
 * lifecycle. Bots, empty slots and clients already torn down have no channel, and each accessor
 * degrades to a null/zero result rather than asserting.
 */
class NetChannels
{
public:
    /** @p interfaces supplies IVEngineServer2; it must outlive this service. */
    explicit NetChannels(Interfaces& interfaces) : _interfaces(interfaces) {}

    /** The client's channel, or nullptr when @p slot has none (bot, empty, disconnecting). */
    INetChannelInfo* GetNetInfo(int slot) const;

    /** Round-trip time in seconds, or 0 when the channel is unavailable. */
    float EngineLatency(int slot) const;

    /**
     * The client's value for the userinfo convar @p name, or empty when the slot has no client,
     * the engine is unavailable, or the client never sent one.
     *
     * The view borrows the engine's own buffer, which the next userinfo update replaces: read it
     * or copy it before returning to the engine, and never store it.
     */
    std::string_view GetUserInfoCvar(int slot, std::string_view name) const;

private:
    Interfaces& _interfaces;
};

}  // namespace VoltMod
