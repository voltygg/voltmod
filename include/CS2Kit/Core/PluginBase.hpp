#pragma once

#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Core/MetamodPluginBase.hpp>
#include <memory>

namespace CS2Kit::Core
{

/**
 * @brief MetamodPluginBase that also owns the plugin's manager container.
 *
 * Constructs @p TManagers after the kit Services are live (so member initializers may call
 * Engine()), publishes it through ActiveService so `App()` works anywhere, and destroys it on
 * unload after the Defer() cleanups ran - the same lifetime the kit's own Services follow.
 *
 * @code
 * // Plugin.hpp
 * class MyPlugin : public CS2Kit::PluginBase<MySystem::Managers> { ... };
 * // Plugin.cpp - instance, PLUGIN_EXPOSE and the App() trampoline in one line:
 * CS2KIT_PLUGIN(MyPlugin, MySystem);
 * @endcode
 */
template <class TManagers>
class PluginBase : public MetamodPluginBase
{
public:
    using ManagersType = TManagers;

    /** The live manager container. Valid only between OnCreateInstances and unload. */
    static TManagers& App() { return ActiveService<TManagers>::Get(); }

    /** The live manager container, or nullptr - for teardown paths that may outlive it. */
    static TManagers* AppOrNull() { return ActiveService<TManagers>::GetOrNull(); }

protected:
    void OnCreateInstances() override
    {
        _managers = std::make_unique<TManagers>();
        ActiveService<TManagers>::Set(_managers.get());
    }

    void OnDestroyInstances() override
    {
        ActiveService<TManagers>::Set(nullptr);
        _managers.reset();
    }

private:
    std::unique_ptr<TManagers> _managers;
};

}  // namespace CS2Kit::Core

/**
 * @brief The per-plugin entry-point boilerplate in one statement: the global instance
 * (g_<PluginClass>), Metamod's PLUGIN_EXPOSE, and the AppNamespace::App() trampoline.
 *
 * Invoke once, at global namespace scope, in the plugin's Plugin.cpp. @p AppNamespace
 * must declare `Managers& App();` over the PluginBase container (the scaffold does).
 */
#define CS2KIT_PLUGIN(PluginClass, AppNamespace) \
    PluginClass g_##PluginClass;                 \
    PLUGIN_EXPOSE(PluginClass, g_##PluginClass); \
    namespace AppNamespace                       \
    {                                            \
    PluginClass::ManagersType& App()             \
    {                                            \
        return PluginClass::App();               \
    }                                            \
    }                                            \
    static_assert(true, "CS2KIT_PLUGIN requires a trailing semicolon")
