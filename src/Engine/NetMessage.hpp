#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <functional>
#include <string_view>

namespace VoltMod
{

/**
 * Send one user message: resolve its type, allocate it, let @p fill populate it, post it to
 * @p filter, and deallocate it.
 *
 * The deallocate has to pair with the allocate on every exit path, including the one where the
 * protobuf cast fails. Getting that wrong leaks a net message per send, which is why every
 * sender goes through here instead of repeating the sequence.
 *
 * @param cached the caller's cache of the resolved message type, filled on the first call.
 *        Message types are stable for the process, so this is resolved once per sender.
 * @param partialName matched with FindNetworkMessagePartial, e.g. "TextMsg" or "VoteStart".
 * @param fill returns false to abandon the send; the message is still deallocated.
 * @return whether the message was posted.
 */
bool PostUserMessage(Interfaces& interfaces, INetworkMessageInternal*& cached, std::string_view partialName,
                     IRecipientFilter& filter, const std::function<bool(CNetMessage*)>& fill);

}  // namespace VoltMod
