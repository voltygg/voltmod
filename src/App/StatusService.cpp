#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <format>
#include <map>
#include <string>
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
    return !_healthy || _healthy();
}

std::string StatusService::BuildJson() const
{
    // Providers already return JSON. Replace invalid payloads so one section cannot corrupt the report.
    std::map<std::string, glz::raw_json> out;
    for (const auto& [name, provider] : _sections)
    {
        std::string section = provider();
        out[name] = glz::validate_json(section) ? glz::raw_json{R"({"error":"provider returned invalid JSON"})"}
                                                : glz::raw_json{std::move(section)};
    }
    out["healthy"] = glz::raw_json{IsHealthy() ? "true" : "false"};
    return Json::Write(out);
}

std::string StatusService::BuildText() const
{
    std::string out;
    for (const auto& [name, provider] : _sections)
    {
        out += name;
        out += ":\n";
        const auto section = Json::ParseDocument(provider());
        if (!section)
        {
            out += "  <invalid JSON from provider>\n";
        }
        else if (section->is_object())
        {
            for (const auto& [key, value] : section->get_object())
            {
                std::string rendered = value.is_string() ? value.get_string() : value.dump().value_or(std::string{});
                out += std::format("  {}: {}\n", key, rendered);
            }
        }
        else
        {
            out += std::format("  {}\n", section->dump().value_or(std::string{}));
        }
    }
    if (!out.empty() && out.back() == '\n')
    {
        out.pop_back();
    }
    return out;
}

void StatusService::InstallCommand(std::string_view name, std::string_view helpText, HealthCheck healthy)
{
    _healthy = std::move(healthy);
    // The member-owned command unregisters before destruction.
    _command.emplace(name, helpText, [this, name = std::string(name)](const CCommand& args, int) {
        if (args.ArgC() > 1 && std::string_view(args.Arg(1)) == "json")
        {
            Msg("STATUS_JSON %s\n", BuildJson().c_str());
            return;
        }
        Msg("=== %s (healthy: %s) ===\n%s\n", name.c_str(), IsHealthy() ? "yes" : "no", BuildText().c_str());
    });
}

}  // namespace VoltMod
