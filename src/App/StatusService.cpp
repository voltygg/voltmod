#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Core/Json.hpp>
#include <format>
#include <string_view>
#include <tier0/dbg.h>
#include <tier1/convar.h>

namespace VoltMod
{

void StatusService::RegisterSection(std::string name, Provider provider)
{
    for (auto& [existing, existingProvider] : _sections)
    {
        if (existing == name)
        {
            existingProvider = std::move(provider);
            return;
        }
    }
    _sections.emplace_back(std::move(name), std::move(provider));
}

bool StatusService::IsHealthy() const
{
    if (!_loadReport.FirstFailure().empty())
        return false;
    return !_healthy || _healthy();
}

std::string StatusService::BuildJson() const
{
    // Providers are parsed, not spliced: a section has to be a value in this object, and its
    // text is only as trustworthy as the provider. A parse failure yields json::discarded, which
    // dump() writes as a bare <discarded> token - that would make the whole STATUS_JSON line
    // unparseable for the tooling it exists for, so name the bad section instead.
    auto out = nlohmann::json::object();
    for (const auto& [name, provider] : _sections)
    {
        auto section = nlohmann::json::parse(provider(), nullptr, /*allow_exceptions=*/false);
        if (section.is_discarded())
            section = nlohmann::json{{"error", "provider returned invalid JSON"}};
        out[name] = std::move(section);
    }
    out["healthy"] = IsHealthy();
    return out.dump();
}

std::string StatusService::BuildText() const
{
    std::string out;
    for (const auto& [name, provider] : _sections)
    {
        out += name;
        out += ":\n";
        const auto section = nlohmann::json::parse(provider(), nullptr, /*allow_exceptions=*/false);
        if (section.is_discarded())
            out += "  <invalid JSON from provider>\n";
        else if (section.is_object())
        {
            for (const auto& [key, value] : section.items())
                out += std::format("  {}: {}\n", key, value.is_string() ? value.get<std::string>() : value.dump());
        }
        else
        {
            out += std::format("  {}\n", section.dump());
        }
    }
    if (!out.empty() && out.back() == '\n')
        out.pop_back();
    return out;
}

void StatusService::InstallCommand(const char* name, const char* helpText, HealthCheck healthy)
{
    _healthy = std::move(healthy);
    // Capturing `this` is safe: the command is a member, so it unregisters before the service goes.
    _command = std::make_unique<ServerCommand>(name, helpText, [this, name](const CCommand& args) {
        if (args.ArgC() > 1 && std::string_view(args.Arg(1)) == "json")
        {
            // Single marker-prefixed line so RCON tooling can find it amid console noise.
            Msg("STATUS_JSON %s\n", BuildJson().c_str());
            return;
        }
        Msg("=== %s (healthy: %s) ===\n%s\n", name, IsHealthy() ? "yes" : "no", BuildText().c_str());
    });
}

}  // namespace VoltMod
