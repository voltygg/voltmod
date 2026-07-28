#pragma once

class INetChannelInfo;

namespace CS2Kit::Sdk
{

/**
 * @brief Per-client network-channel access: latency and the client's replicated userinfo cvars.
 *
 * Stateless wrapper over IVEngineServer2 - every call reads the engine live, so nothing here
 * needs a lifecycle. Bots, empty slots, and clients whose channel is already torn down have no
 * channel; each accessor degrades to a null/zero result rather than asserting.
 */
class NetChannelService
{
public:
    /** The client's channel, or nullptr when @p slot has none (bot, empty, disconnecting). */
    INetChannelInfo* GetNetInfo(int slot) const;

    /** Round-trip time in seconds, or 0 when the channel is unavailable. */
    float EngineLatency(int slot) const;

    /**
     * @brief The client's current value for the replicated userinfo cvar @p name.
     *
     * Only cvars the client replicates (FCVAR_USERINFO - `name`, `sensitivity`, `m_yaw`,
     * `cl_interp_ratio`, ...) are visible this way; anything else needs a cvar query.
     * @return Engine-owned string, valid until the next engine call; nullptr/empty when unset.
     */
    const char* GetUserInfoCvar(int slot, const char* name) const;
};

}  // namespace CS2Kit::Sdk
