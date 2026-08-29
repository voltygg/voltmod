#pragma once

// Database APIs. This header is separate because it includes <pqxx/pqxx>.

#ifndef VOLTMOD_ENABLE_POSTGRES
#error "VoltMod/Database requires the framework to be built with VOLTMOD_ENABLE_POSTGRES"
#endif

#include <VoltMod/Database/DbResult.hpp>
#include <VoltMod/Database/Mapping.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/PostgresDatabase.hpp>
