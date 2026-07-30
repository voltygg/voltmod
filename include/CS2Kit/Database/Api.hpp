#pragma once

// Database vocabulary hoist, split out of <CS2Kit/Api.hpp> so <pqxx/pqxx>
// only reaches TUs that opt in. Include this header wherever the short
// CS2Kit::PostgresDatabase / CS2Kit::FromRow / ... spellings are used.

#ifndef CS2KIT_ENABLE_POSTGRES
#error "CS2Kit/Database requires the kit to be built with CS2KIT_ENABLE_POSTGRES"
#endif

#include <CS2Kit/Database/DbResult.hpp>
#include <CS2Kit/Database/Mapping.hpp>
#include <CS2Kit/Database/Migrator.hpp>
#include <CS2Kit/Database/PostgresDatabase.hpp>

namespace CS2Kit
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

}  // namespace CS2Kit
