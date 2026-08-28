#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <string_view>

namespace VoltMod
{

/**
 * @file ProtoReflect.hpp
 * @brief Reaching a CS user message's fields by protobuf reflection.
 *
 * The CS-specific user messages (`CCSUsrMsg_VoteStart`, `CCSUsrMsg_CustomHudClicked` and friends)
 * are declared in the SDK's cstrike15_usermessages.proto but are not generated into headers, and
 * consumer builds deliberately do not run protoc. The engine has registered their descriptors
 * though, so their fields are reached by name instead - no generated type, no build-system change,
 * and a renamed field degrades at runtime rather than miscompiling.
 *
 * Nothing here logs, because what a missing field costs is the caller's to say: a vote panel
 * renders incomplete, a HUD press is unreadable. Internal to `src/`.
 */

using ProtoMessage = google::protobuf::Message;
using ProtoFieldDescriptor = google::protobuf::FieldDescriptor;

/** @p name's descriptor on @p message, or nullptr when the message carries no such field. */
const ProtoFieldDescriptor* ProtoField(const ProtoMessage& message, std::string_view name);

}  // namespace VoltMod
