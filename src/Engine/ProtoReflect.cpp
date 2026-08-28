#include "Engine/ProtoReflect.hpp"

#include <string>

namespace VoltMod
{

const ProtoFieldDescriptor* ProtoField(const ProtoMessage& message, std::string_view name)
{
    const auto* descriptor = message.GetDescriptor();
    // protobuf's ConstStringParam is a std::string on this build, so the view is materialized.
    return descriptor ? descriptor->FindFieldByName(std::string(name)) : nullptr;
}

}  // namespace VoltMod
