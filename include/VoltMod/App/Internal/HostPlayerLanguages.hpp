#pragma once

#include <VoltMod/Core/Text/PlayerLanguages.hpp>
#include <VoltMod/Host/IHostLanguages.hpp>
#include <string_view>

namespace VoltMod::Internal
{

/** @ref PlayerLanguages over the host's table, which Core cannot include. */
class HostPlayerLanguages final : public PlayerLanguages
{
public:
    explicit HostPlayerLanguages(IHostLanguages& host) : _host(host) {}

    std::string_view Language(int slot) const override { return _host.Language(slot); }
    void SetLanguage(int slot, std::string_view lang) override { _host.SetLanguage(slot, lang); }

private:
    IHostLanguages& _host;
};

}  // namespace VoltMod::Internal
