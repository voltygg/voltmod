#pragma once

#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Build text and JSON diagnostics from named providers.
 *
 * Providers run on demand in registration order. VoltMod registers framework sections during
 * load; plugins add their sections in Load and expose the combined report with
 * @ref InstallCommand. A provider returns compact JSON text, normally built with
 * `VoltMod::Json::Write` from `<VoltMod/Core/Text/Json.hpp>`.
 */
class StatusService
{
public:
    /** @brief Returns this section's payload as JSON text (an object or scalar). */
    using Provider = std::function<std::string()>;

    /** @brief Optional plugin-specific health predicate. */
    using HealthCheck = std::function<bool()>;

    /** @brief Add a section, replacing any existing one with the same name. */
    void RegisterSection(std::string name, Provider provider);

    /** @brief The registered health result, or true when no predicate is registered. */
    bool IsHealthy() const;

    /** @brief One JSON object with a key per section, plus a top-level `healthy` flag. */
    std::string BuildJson() const;

    /** @brief Human-readable multi-line rendering of the same sections. */
    std::string BuildText() const;

    /**
     * @brief Install the server command that reports this status.
     *
     * `<name>` prints @ref BuildText. `<name> json` emits @ref BuildJson as one `STATUS_JSON {...}`
     * line for RCON tooling. The command unregisters when this service is destroyed.
     */
    void InstallCommand(std::string_view name, std::string_view helpText, HealthCheck healthy = {});

private:
    std::vector<std::pair<std::string, Provider>> _sections;
    HealthCheck _healthy;
    std::optional<ServerCommand> _command;
};

}  // namespace VoltMod
