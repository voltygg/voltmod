#pragma once

// The Hooks module's public surface in one include: engine hooks a plugin subscribes to,
// plus the game-event and messaging types most hook handlers need to react (Events and
// Messaging are included by name here, not wholesale - Hooks does not depend on the rest
// of Messaging, such as ChatColors). Include the individual headers when a translation
// unit only needs a few of these.

#include <VoltMod/Events/EventTypes.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Hooks/ClientCvars.hpp>
#include <VoltMod/Hooks/Addons.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <VoltMod/Hooks/UserCmd.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Hooks/Vote.hpp>
#include <VoltMod/Messaging/CenterHtml.hpp>
#include <VoltMod/Messaging/Messages.hpp>
