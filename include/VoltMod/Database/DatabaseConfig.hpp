#pragma once

#include <string>

namespace VoltMod
{

/**
 * @brief Connection parameters for every supported backend.
 *
 * Field names are lowercase so a consumer's JSON config section maps onto them directly:
 * reflection reads the members, so a plugin needs no mapper - embedding this in a settings
 * struct is all there is to do.
 *
 * The dependency-free header keeps settings structs usable in SDK-free tests and translation
 * units that do not link a database client.
 */
struct DatabaseConfig
{
    /** "postgres", "mariadb" or "sqlite"; anything else fails `Database::Connect`. */
    std::string driver = "postgres";
    std::string host = "localhost";
    /** 0 uses the driver default (5432 for Postgres, 3306 for MariaDB). Ignored by sqlite. */
    int port = 0;
    std::string database = "voltmod_server";
    std::string username = "voltmod_plugin";
    std::string password;
    /** Postgres sslmode: disable, allow, prefer, require, verify-ca or verify-full. MariaDB
     *  turns TLS on for require and the verify modes; sqlite ignores it. */
    std::string sslMode = "prefer";
    /** Bounds every (re)connect attempt so a dead database can't hang queries or unload. */
    int connectTimeoutSec = 5;
    /** sqlite database file, relative to the game dir. ":memory:" is allowed. */
    std::string path;
};

}  // namespace VoltMod
