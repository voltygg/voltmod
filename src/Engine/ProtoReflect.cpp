#include "Engine/ProtoReflect.hpp"

namespace VoltMod
{

const ProtoFieldDescriptor* ProtoField(const ProtoMessage& message, const char* name)
{
    const auto* descriptor = message.GetDescriptor();
    return descriptor ? descriptor->FindFieldByName(name) : nullptr;
}

}  // namespace VoltMod
