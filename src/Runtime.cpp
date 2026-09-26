#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Menu/PanoramaMenu.hpp>
#include <VoltMod/Players/Permissions.hpp>
#include <VoltMod/Runtime.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

Runtime::Runtime(IHost& host, UnsafeServices& unsafe)
    : PluginName(host.Name()),
      Version(host.Version()),
      _languages(host.Languages()),
      Unsafe(unsafe),
      Exchange(host.Services()),
      Commands(Policy, Translations, Players, Entities, Messages, host)
{
    Policy.HasPermission = [this, warned = false](int64_t steamId, std::string_view permission) mutable {
        if (auto* permissions = Exchange.Get<IPermissions>())
        {
            return permissions->HasPermission(steamId, permission);
        }
        if (!warned)
        {
            warned = true;
            Log::Warn("Denying '{}': no plugin publishes {}.", permission,
                      IPermissions::InterfaceName);
        }
        return false;
    };

    RecordServiceSteps();
    RegisterStatusSections();
}

Runtime::~Runtime()
{
    // Stop HTTP workers and flush queued logs before removing frame hooks.
    Http.Stop();
    Log::DeliverPending();
}

std::string Runtime::PluginFile(std::string_view relative) const
{
    return VoltMod::PluginFile(PluginName, relative);
}

Subscription Runtime::UsePanorama(PanoramaMenuLayout& layout, uint64_t addonId)
{
    auto menu = std::make_unique<PanoramaMenu>(PanoramaMenu::Services{.Scheduler = Scheduler,
                                                                      .Slots = Slots,
                                                                      .Freeze = Freeze,
                                                                      .ChatInput = Hooks.ChatInput,
                                                                      .Translations = Translations,
                                                                      .Policy = Policy,
                                                                      .Screens = Screens,
                                                                      .Addons = Addons},
                                               layout, addonId);
    Subscription preferred = Menus.Prefer(*menu);
    // Stop routing to the menu before destroying it.
    return Subscription([menu = std::move(menu), preferred = std::move(preferred)]() mutable {
        preferred.Reset();
        menu.reset();
    });
}

void Runtime::RecordServiceSteps()
{
    LoadSteps.Optional("GameData", [this] { return Unsafe.GameData; });
    LoadSteps.Required("Messages", [this] { return Messages.Available(); });
    LoadSteps.Optional("Entities", [this] { return Entities.Available(); });
    LoadSteps.Optional("ConVars", [this] { return ConVars.Available(); });
    LoadSteps.Optional("GameEvents", [this] { return GameEvents.Available(); });
    LoadSteps.Optional("ClientConVars", [this] { return Hooks.ClientConVars.Available(); });

    for (const auto& [feature, reason] : UnavailableFeatures())
    {
        Log::Warn("{} is unavailable: {}", feature, reason);
    }
}

std::map<std::string, std::string> Runtime::UnavailableFeatures() const
{
    const std::pair<std::string_view, VoltMod::Status> features[] = {
        {"Movement", Hooks.Movement.Available()},
        {"Teleport", Hooks.Teleport.Available()},
        {"Visibility", Hooks.Visibility.Available()},
        {"Trace", Trace.Available()},
        {"Spawning", Entities.Available()},
        {"ClientConVars", Hooks.ClientConVars.Available()},
        {"Screens", Screens.Available()},
        {"Damage", Hooks.Damage.Available()},
    };

    std::map<std::string, std::string> unavailable;
    for (const auto& [feature, available] : features)
    {
        if (!available)
        {
            unavailable.emplace(feature, available.error().Detail);
        }
    }
    return unavailable;
}

void Runtime::RegisterStatusSections()
{
    // Plugins add status sections during Load; the runtime owns them for the load cycle.
    Status.RegisterSection("build", [this] { return Json::Write(glz::obj{"name", PluginName, "version", Version}); });

    Status.RegisterSection("load", [this] {
        std::map<std::string, std::string> failed;
        for (const FailedStep& step : LoadSteps.Failures())
        {
            failed.emplace(step.Name, step.Reason);
        }
        return Json::Write(
            glz::obj{"steps", LoadSteps.Count(), "failed", failed, "unavailable", UnavailableFeatures()});
    });

    Status.RegisterSection("uptime", [start = std::chrono::steady_clock::now()] {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return Json::Write(glz::obj{"seconds", uptime.count()});
    });
}

void Runtime::OnGameFrame()
{
    Log::DeliverPending();
    Scheduler.OnGameFrame();
}

}  // namespace VoltMod
