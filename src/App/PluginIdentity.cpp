#include <VoltMod/App/PluginManifest.hpp>
#include <VoltMod/App/StandardLoad.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>
#include <fstream>
#include <sstream>

namespace VoltMod
{

static void ReportDependency(ServiceExchange& exchange, const PluginDependency& dependency)
{
    const std::string key = IdentityKey(dependency.Name);
    auto* peer = static_cast<IPluginIdentity*>(exchange.GetNamed(key.c_str()));

    if (peer == nullptr)
    {
        const std::string message = std::format("dependency '{}' is not loaded", dependency.Name);
        if (dependency.Required)
            Log::Error("{}.", message);
        else
            Log::Warn("{}; features that need it stay off.", message);
        return;
    }

    const char* version = peer->PluginVersion();
    if (!VersionAtLeast(version ? version : "", dependency.MinVersion))
    {
        const std::string message =
            std::format("dependency '{}' is {} but {} or newer is expected", dependency.Name,
                        (version && *version) ? version : "an unknown version", dependency.MinVersion);
        if (dependency.Required)
            Log::Error("{}.", message);
        else
            Log::Warn("{}.", message);
    }
}

void PluginIdentity::Adopt(PluginManifest manifest)
{
    _manifest = std::move(manifest);
    _key = IdentityKey(_manifest.Name);
    _exchange.PublishNamed(_key.c_str(), static_cast<IPluginIdentity*>(this));

    if (_manifest.Dependencies.empty())
        return;

    // First frame, not OnLoad - see PluginIdentity's comment.
    _scheduler.NextTick([this] {
        for (const auto& dependency : _manifest.Dependencies)
            ReportDependency(_exchange, dependency);
    });
}

void PluginIdentity::Withdraw()
{
    if (_key.empty())
        return;
    _exchange.UnpublishNamed(_key.c_str());
    _key.clear();
}

void LoadPluginManifest(Runtime& runtime, std::string_view addon)
{
    const std::string path = AddonFile(addon, std::format("{}.manifest.json", addon));

    runtime.LoadReport.Run("Manifest", [&]() -> StageResult {
        // ResolvePath, like every other addon file: the server's working directory is
        // game/bin/win64, not the game dir addon paths are relative to.
        std::ifstream file(ResolvePath(path), std::ios::binary);
        if (!file)
            return StageResult::Degraded("no manifest shipped");

        std::ostringstream buffer;
        buffer << file.rdbuf();

        auto manifest = ParsePluginManifest(buffer.str());
        if (!manifest)
            return StageResult::Degraded(std::format("{} is malformed", path));

        const size_t dependencies = manifest->Dependencies.size();
        const std::string version = manifest->Version;
        runtime.Identity.Adopt(std::move(*manifest));
        return StageResult::Ok(dependencies == 0 ? std::format("v{}", version)
                                                 : std::format("v{}, {} dependencies", version, dependencies));
    });
}

}  // namespace VoltMod
