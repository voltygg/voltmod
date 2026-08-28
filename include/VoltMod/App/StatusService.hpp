#pragma once

#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Engine/ServerCommand.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Aggregates named status sections for diagnostics commands.
 *
 * VoltMod registers its own sections (build, load, gamedata, uptime) during load;
 * plugins add theirs in OnLoad and expose the report with @ref InstallCommand.
 * Providers run on demand, in registration order. Each provider returns its section's
 * payload as compact JSON text - build it with `VoltMod::Json` or `nlohmann::json{...}.dump()`
 * (`<VoltMod/Core/Json.hpp>`) - so this header, reachable from every `<VoltMod/Api.hpp>`
 * translation unit through Runtime, never has to include nlohmann itself.
 */
class StatusService
{
public:
    /** @brief Returns this section's payload as JSON text (an object or scalar). */
    using Provider = std::function<std::string()>;

    /** @brief Plugin health condition, ANDed with the baseline (no Failed load stage). */
    using HealthCheck = std::function<bool()>;

    /** @p loadReport supplies the baseline health signal and must outlive this service. */
    explicit StatusService(LoadReport& loadReport) : _loadReport(loadReport) {}

    /** @brief Add a section, replacing any existing one with the same name. */
    void RegisterSection(std::string name, Provider provider);

    /** @brief True when no load stage Failed and the plugin's HealthCheck (if any) agrees. */
    bool IsHealthy() const;

    /** @brief One JSON object with a key per section, plus a top-level `healthy` flag. */
    std::string BuildJson() const;

    /** @brief Human-readable multi-line rendering of the same sections. */
    std::string BuildText() const;

    /**
     * @brief Install the server command that reports this status.
     *
     * `<name>` prints @ref BuildText for humans; `<name> json` emits @ref BuildJson as one
     * `STATUS_JSON {...}` line that RCON tooling can find amid console noise. `name` and
     * `helpText` must outlive the plugin (tier1 keeps the pointers); the command belongs to
     * this service and unregisters when the services are torn down.
     */
    void InstallCommand(std::string_view name, std::string_view helpText, HealthCheck healthy = {});

private:
    LoadReport& _loadReport;
    std::vector<std::pair<std::string, Provider>> _sections;
    HealthCheck _healthy;
    std::unique_ptr<ServerCommand> _command;
};

}  // namespace VoltMod
