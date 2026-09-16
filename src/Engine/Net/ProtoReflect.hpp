#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <string_view>

namespace VoltMod
{

/**
 * @file ProtoReflect.hpp
 * @brief Access CS user-message fields through protobuf reflection.
 *
 * Consumer builds do not generate the CS-specific messages from cstrike15_usermessages.proto.
 * Their engine-registered descriptors still support name-based lookup. A renamed field therefore
 * appears missing at runtime, and each caller chooses its fallback.
 */

using ProtoMessage = google::protobuf::Message;
using ProtoFieldDescriptor = google::protobuf::FieldDescriptor;

/** Descriptor for @p name, or nullptr when the field is absent. */
const ProtoFieldDescriptor* ProtoField(const ProtoMessage& message, std::string_view name);

}  // namespace VoltMod
