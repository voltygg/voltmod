#include <Color.h>
#include <VoltMod/Entities/KeyValues.hpp>
#include <entity2/entitykeyvalues.h>
#include <mathlib/vector.h>
#include <string>

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

// CEntityKeyValues interns the key and copies the value, so the NUL-terminated temporaries below
// only have to survive the call.
KeyValues& KeyValues::Set(std::string_view key, std::string_view value)
{
    if (_kv)
        _kv->SetString(std::string(key).c_str(), std::string(value).c_str());
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, int value)
{
    if (_kv)
        _kv->SetInt(std::string(key).c_str(), value);
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, float value)
{
    if (_kv)
        _kv->SetFloat(std::string(key).c_str(), value);
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, bool value)
{
    if (_kv)
        _kv->SetBool(std::string(key).c_str(), value);
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, const Vector& value)
{
    if (_kv)
        _kv->SetVector(std::string(key).c_str(), value);
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, const QAngle& value)
{
    if (_kv)
        _kv->SetQAngle(std::string(key).c_str(), value);
    return *this;
}

KeyValues& KeyValues::Set(std::string_view key, const Color& value)
{
    if (_kv)
        _kv->SetColor(std::string(key).c_str(), value);
    return *this;
}

CEntityKeyValues* KeyValues::Detach()
{
    CEntityKeyValues* kv = _kv;
    _kv = nullptr;
    return kv;
}

}  // namespace VoltMod
