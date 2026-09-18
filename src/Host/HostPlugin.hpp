#pragma once

#include "Host/EngineHooks.hpp"
#include "Host/HostGameData.hpp"
#include "Host/HostSchema.hpp"
#include "Host/PluginHost.hpp"
#include "Host/PluginLoader.hpp"

#include <ISmmPlugin.h>

#include <cstddef>
#include <memory>

namespace VoltMod
{

/**
 * @brief The one Metamod plugin in the process.
 *
 * Everything else is a plugin library this loads itself, so Metamod sees a single module and the
 * engine hooks are installed once. Cross-plugin lookup is the host's own service table, not
 * Metamod's factory, which is why there is no OnMetamodQuery here.
 */
class HostPlugin final : public ISmmPlugin, public IMetamodListener
{
public:
    HostPlugin();
    ~HostPlugin() override;

    HostPlugin(const HostPlugin&) = delete;
    HostPlugin& operator=(const HostPlugin&) = delete;

    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;

    const char* GetAuthor() override;
    const char* GetName() override;
    const char* GetDescription() override;
    const char* GetURL() override;
    const char* GetLicense() override;
    const char* GetVersion() override;
    const char* GetDate() override;
    const char* GetLogTag() override;

private:
    /** Take the plugins down, then the hooks, then the host's own state. */
    void Shutdown();

    // Declared in build order so destruction runs backwards: the hooks stop first, so nothing
    // reaches a plugin while the loader is unloading it.
    std::unique_ptr<HostGameData> _gameData;
    std::unique_ptr<PluginHost> _host;
    std::unique_ptr<HostSchema> _schema;
    std::unique_ptr<PluginLoader> _plugins;
    std::unique_ptr<EngineHooks> _hooks;
};

}  // namespace VoltMod
