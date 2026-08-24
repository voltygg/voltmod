#pragma once

// Database vocabulary hoist, split out of <VoltMod/Api.hpp> so <pqxx/pqxx>
// only reaches TUs that opt in. Include this header wherever the short
// VoltMod::PostgresDatabase / VoltMod::FromRow / ... spellings are used.

#ifndef VOLTMOD_ENABLE_POSTGRES
#error "VoltMod/Database requires the framework to be built with VOLTMOD_ENABLE_POSTGRES"
#endif

#include <VoltMod/Database/DbResult.hpp>
#include <VoltMod/Database/Mapping.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/PostgresDatabase.hpp>

namespace VoltMod
{

using Database::Column;
using Database::DbResult;
using Database::FromResult;
using Database::FromRow;
using Database::InsertParams;
using Database::InsertSql;
using Database::MigrationResult;
using Database::PostgresConfig;
using Database::PostgresDatabase;
using Database::RunMigrations;
using Database::SelectSql;
using Database::TryDb;
using Database::TryOr;

}  // namespace VoltMod
