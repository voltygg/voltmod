#include <CS2Kit/Core/JsonConfig.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/PluginManifest.hpp>
#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Core/StandardLoad.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <format>
#include <fstream>
#include <sstream>

namespace Log = CS2Kit::Utils::Log;

namespace CS2Kit::Core
{

namespace
{
/** One dependency's state, once every plugin has had a chance to load. */
void ReportDependency(const PluginDependency& dependency)
{
    const std::string key = IdentityKey(dependency.Name);
    auto* peer = static_cast<IPluginIdentity*>(Engine().Exchange.GetNamed(key.c_str()));

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
}  // namespace

void PluginIdentity::Adopt(PluginManifest manifest)
{
    _manifest = std::move(manifest);
    _key = IdentityKey(_manifest.Name);
    Engine().Exchange.PublishNamed(_key.c_str(), static_cast<IPluginIdentity*>(this));

    if (_manifest.Dependencies.empty())
        return;

    // First frame, not OnLoad: see the class comment - at OnLoad a peer that simply loads
    // later looks exactly like one that is missing.
    Engine().Scheduler.NextTick([this] {
        for (const auto& dependency : _manifest.Dependencies)
            ReportDependency(dependency);
    });
}

void PluginIdentity::Withdraw()
{
    if (_key.empty())
        return;
    Engine().Exchange.UnpublishNamed(_key.c_str());
    _key.clear();
}

void LoadPluginManifest(std::string_view addon)
{
    const std::string path = AddonFile(addon, std::format("{}.manifest.json", addon));

    Engine().LoadReport.Run("Manifest", [&]() -> StageResult {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return StageResult::Degraded("no manifest shipped");

        std::ostringstream buffer;
        buffer << file.rdbuf();

        auto manifest = ParsePluginManifest(buffer.str());
        if (!manifest)
            return StageResult::Degraded(std::format("{} is malformed", path));

        const size_t dependencies = manifest->Dependencies.size();
        const std::string version = manifest->Version;
        Engine().Identity.Adopt(std::move(*manifest));
        return StageResult::Ok(dependencies == 0 ? std::format("v{}", version)
                                                 : std::format("v{}, {} dependencies", version, dependencies));
    });
}

}  // namespace CS2Kit::Core
