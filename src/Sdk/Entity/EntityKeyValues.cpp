#include <Color.h>
#include <VoltMod/Sdk/Entity/EntityKeyValues.hpp>
#include <entity2/entitykeyvalues.h>
#include <mathlib/vector.h>

namespace VoltMod::Sdk
{

EntityKeyValues::EntityKeyValues() : _kv(new CEntityKeyValues()) {}

EntityKeyValues::~EntityKeyValues()
{
    // The refcount starts at 0 and Release() deletes at <= 0, so this frees a
    // never-spawned object; after Detach() there is nothing left to free here.
    if (_kv)
        _kv->Release();
}

EntityKeyValues& EntityKeyValues::Set(const char* key, const char* value)
{
    if (_kv)
        _kv->SetString(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, int value)
{
    if (_kv)
        _kv->SetInt(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, float value)
{
    if (_kv)
        _kv->SetFloat(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, bool value)
{
    if (_kv)
        _kv->SetBool(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, const Vector& value)
{
    if (_kv)
        _kv->SetVector(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, const QAngle& value)
{
    if (_kv)
        _kv->SetQAngle(key, value);
    return *this;
}

EntityKeyValues& EntityKeyValues::Set(const char* key, const Color& value)
{
    if (_kv)
        _kv->SetColor(key, value);
    return *this;
}

CEntityKeyValues* EntityKeyValues::Detach()
{
    CEntityKeyValues* kv = _kv;
    _kv = nullptr;
    return kv;
}

}  // namespace VoltMod::Sdk
