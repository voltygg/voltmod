#include <VoltMod/Core/Capabilities.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

bool Capabilities::Has(Capability capability) const noexcept
{
    const size_t index = EnumIndex(capability);
    return index < _entries.size() && _entries[index].Ok;
}

std::string_view Capabilities::Reason(Capability capability) const noexcept
{
    const size_t index = EnumIndex(capability);
    if (index >= _entries.size() || _entries[index].Ok)
        return {};
    return _entries[index].Reason;
}

void Capabilities::Set(Capability capability, bool ok, std::string reason)
{
    const size_t index = EnumIndex(capability);
    if (index >= _entries.size())
        return;

    _entries[index].Ok = ok;
    _entries[index].Reason = ok ? std::string{} : std::move(reason);
}

std::string Capabilities::Summary() const
{
    size_t ok = 0;
    std::string missing;
    for (Capability capability : EnumValues<Capability>())
    {
        const size_t index = EnumIndex(capability);
        if (_entries[index].Ok)
        {
            ++ok;
            continue;
        }
        missing += std::format("; {}: {}", Name(capability), _entries[index].Reason);
    }

    return std::format("{}/{} ok{}", ok, _entries.size(), missing);
}

}  // namespace VoltMod
