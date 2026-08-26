#include <Color.h>
#include <VoltMod/Entities/KeyValues.hpp>
#include <entity2/entitykeyvalues.h>
#include <mathlib/vector.h>

namespace VoltMod
{

KeyValues::KeyValues() : _kv(new CEntityKeyValues()) {}

KeyValues::~KeyValues()
{
    // The refcount starts at 0 and Release() deletes at <= 0, so this frees a
    // never-spawned object; after Detach() there is nothing left to free here.
    if (_kv)
        _kv->Release();
}

KeyValues& KeyValues::Set(const char* key, const char* value)
{
    if (_kv)
        _kv->SetString(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, int value)
{
    if (_kv)
        _kv->SetInt(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, float value)
{
    if (_kv)
        _kv->SetFloat(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, bool value)
{
    if (_kv)
        _kv->SetBool(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, const Vector& value)
{
    if (_kv)
        _kv->SetVector(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, const QAngle& value)
{
    if (_kv)
        _kv->SetQAngle(key, value);
    return *this;
}

KeyValues& KeyValues::Set(const char* key, const Color& value)
{
    if (_kv)
        _kv->SetColor(key, value);
    return *this;
}

CEntityKeyValues* KeyValues::Detach()
{
    CEntityKeyValues* kv = _kv;
    _kv = nullptr;
    return kv;
}

}  // namespace VoltMod
