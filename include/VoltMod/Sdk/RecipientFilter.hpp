#pragma once

#include <irecipientfilter.h>

namespace VoltMod::Sdk
{

/**
 * @brief Routes a user message to a single player slot.
 *
 * IGameEventSystem exposes two PostEventAbstract overloads; the IRecipientFilter form is the
 * reliable one. The raw client-bitmask overload silently dropped server-originated messages on
 * current CS2 builds, so pass one of these filters instead.
 */
class SingleRecipientFilter : public IRecipientFilter
{
public:
    explicit SingleRecipientFilter(int slot)
    {
        if (slot >= 0 && slot < ABSOLUTE_PLAYER_LIMIT)
            _recipients.Set(slot);
    }

    NetChannelBufType_t GetNetworkBufType() const override { return BUF_RELIABLE; }
    bool IsInitMessage() const override { return false; }
    const CPlayerBitVec& GetRecipients() const override { return _recipients; }
    CPlayerSlot GetPredictedPlayerSlot() const override { return CPlayerSlot(-1); }

private:
    CPlayerBitVec _recipients;
};

/**
 * @brief Routes a user message or filtered sound to a chosen set of player slots.
 * Starts empty; callers add each recipient (e.g. every connected slot for a broadcast).
 */
class MultiRecipientFilter : public IRecipientFilter
{
public:
    MultiRecipientFilter() = default;

    void AddRecipient(int slot)
    {
        if (slot >= 0 && slot < ABSOLUTE_PLAYER_LIMIT)
            _recipients.Set(slot);
    }

    NetChannelBufType_t GetNetworkBufType() const override { return BUF_RELIABLE; }
    bool IsInitMessage() const override { return false; }
    const CPlayerBitVec& GetRecipients() const override { return _recipients; }
    CPlayerSlot GetPredictedPlayerSlot() const override { return CPlayerSlot(-1); }

private:
    CPlayerBitVec _recipients;
};

}  // namespace VoltMod::Sdk
