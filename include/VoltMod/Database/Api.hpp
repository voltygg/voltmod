#pragma once

// The database half of the VoltMod vocabulary, split out of <VoltMod/Api.hpp> so
// <pqxx/pqxx> only reaches the translation units that opt in.

#ifndef VOLTMOD_ENABLE_POSTGRES
#error "VoltMod/Database requires the framework to be built with VOLTMOD_ENABLE_POSTGRES"
#endif

#include <VoltMod/Database/DbResult.hpp>
#include <VoltMod/Database/Mapping.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/PostgresDatabase.hpp>
