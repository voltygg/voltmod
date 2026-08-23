#include <CS2Kit/App/StandardLoad.hpp>
#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/PluginManifest.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <format>
#include <fstream>
#include <sstream>

namespace Log = CS2Kit::Core::Log;

namespace CS2Kit::App
{

namespace
{
void ReportDependency(const Core::PluginDependency& dependency)
{
    const std::string key = Core::IdentityKey(dependency.Name);
    auto* peer = static_cast<Core::IPluginIdentity*>(CS2Kit::Detail::Rt().Exchange.GetNamed(key.c_str()));

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
    if (!Core::VersionAtLeast(version ? version : "", dependency.MinVersion))
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

void PluginIdentity::Adopt(Core::PluginManifest manifest)
{
    _manifest = std::move(manifest);
    _key = Core::IdentityKey(_manifest.Name);
    CS2Kit::Detail::Rt().Exchange.PublishNamed(_key.c_str(), static_cast<Core::IPluginIdentity*>(this));

    if (_manifest.Dependencies.empty())
        return;

    // First frame, not OnLoad - see PluginIdentity's comment.
    CS2Kit::Detail::Rt().Scheduler.NextTick([this] {
        for (const auto& dependency : _manifest.Dependencies)
            ReportDependency(dependency);
    });
}

void PluginIdentity::Withdraw()
{
    if (_key.empty())
        return;
    CS2Kit::Detail::Rt().Exchange.UnpublishNamed(_key.c_str());
    _key.clear();
}

void LoadPluginManifest(Runtime& runtime, std::string_view addon)
{
    const std::string path = Core::AddonFile(addon, std::format("{}.manifest.json", addon));

    runtime.LoadReport.Run("Manifest", [&]() -> Core::StageResult {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return Core::StageResult::Degraded("no manifest shipped");

        std::ostringstream buffer;
        buffer << file.rdbuf();

        auto manifest = Core::ParsePluginManifest(buffer.str());
        if (!manifest)
            return Core::StageResult::Degraded(std::format("{} is malformed", path));

        const size_t dependencies = manifest->Dependencies.size();
        const std::string version = manifest->Version;
        runtime.Identity.Adopt(std::move(*manifest));
        return Core::StageResult::Ok(dependencies == 0 ? std::format("v{}", version)
                                                       : std::format("v{}, {} dependencies", version, dependencies));
    });
}

}  // namespace CS2Kit::App
