#pragma once

// The Workshop module's public surface: requiring workshop addons of connecting clients, as leases.
// Runtime holds the Addons service by value, so `<VoltMod/Api.hpp>` already reaches these types;
// include this header where a translation unit means to use them.

#include <VoltMod/Workshop/Addons.hpp>
