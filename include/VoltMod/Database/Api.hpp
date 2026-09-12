#pragma once

// Database APIs. This header is separate because it includes the sqlpp23 connectors.

#ifndef VOLTMOD_ENABLE_DATABASE
#error "VoltMod/Database requires the framework to be built with VOLTMOD_ENABLE_DATABASE"
#endif

#include <VoltMod/Database/Connection.hpp>
#include <VoltMod/Database/Database.hpp>
#include <VoltMod/Database/DatabaseConfig.hpp>
#include <VoltMod/Database/Driver.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/Table.hpp>
