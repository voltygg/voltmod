#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Core/Json.hpp>
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
    if (!_loadReport.FirstFailure().empty())
        return false;
    return !_healthy || _healthy();
}

std::string StatusService::BuildJson() const
{
    // Providers already return serialized text, so their output is spliced in as raw JSON rather
    // than parsed into a document and written back out. Invalid output is replaced so one bad
    // provider cannot invalidate the whole STATUS_JSON line, and names itself for tooling.
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
                // A string reads as itself; anything else is rendered as the JSON it is.
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
        out.pop_back();
    return out;
}

void StatusService::InstallCommand(std::string_view name, std::string_view helpText, HealthCheck healthy)
{
    _healthy = std::move(healthy);
    // Capturing `this` is safe: the command is a member, so it unregisters before the service goes.
    // The name is captured by value - the caller's view does not have to outlive the command.
    _command = std::make_unique<ServerCommand>(name, helpText, [this, name = std::string(name)](const CCommand& args) {
        if (args.ArgC() > 1 && std::string_view(args.Arg(1)) == "json")
        {
            // Single marker-prefixed line so RCON tooling can find it amid console noise.
            Msg("STATUS_JSON %s\n", BuildJson().c_str());
            return;
        }
        Msg("=== %s (healthy: %s) ===\n%s\n", name.c_str(), IsHealthy() ? "yes" : "no", BuildText().c_str());
    });
}

}  // namespace VoltMod
