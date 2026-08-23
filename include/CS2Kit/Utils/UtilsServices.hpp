#pragma once

#include <CS2Kit/Utils/Translations.hpp>

namespace CS2Kit::Utils
{

/**
 * @brief The Utils-layer services, reachable from the layers above.
 *
 * One member today. It exists so Commands, Menu and plugin code can translate without
 * including the composition root - the same reason Core and Sdk have a Ctx().
 */
class UtilsServices
{
public:
    UtilsServices() = default;
    UtilsServices(const UtilsServices&) = delete;
    UtilsServices& operator=(const UtilsServices&) = delete;

    Utils::Translations Translations;
};

/** Set/clear the active UtilsServices. Called by the composition root on Load/Unload. */
void SetActiveUtilsServices(UtilsServices* services);

/** The active UtilsServices. Asserts if called outside a Load/Unload window. */
UtilsServices& Ctx();

/** The active UtilsServices, or nullptr - for teardown paths that may run after Shutdown. */
UtilsServices* CtxOrNull();

}  // namespace CS2Kit::Utils
