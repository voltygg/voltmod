#pragma once

#include <string>

namespace VoltMod
{

/**
 * @brief PostgreSQL connection parameters.
 *
 * Field names are lowercase so a consumer's JSON config section maps onto them directly:
 * reflection reads the members, so a plugin needs no mapper - embedding this in a settings
 * struct is all there is to do.
 *
 * Deliberately its own header, free of libpqxx: a plugin's settings struct embeds this, and that
 * struct is included by ordinary translation units and recompiled by SDK-free tests. Neither
 * should have to compile pqxx to name a host and a port.
 */
struct PostgresConfig
{
    std::string host = "localhost";
    int port = 5432;
    std::string database = "voltmod_server";
    std::string username = "voltmod_plugin";
    std::string password;
    std::string sslMode = "prefer";
    /** Bounds every (re)connect attempt so a dead database can't hang queries or unload. */
    int connectTimeoutSec = 5;

    std::string GetConnectionString() const;
};

}  // namespace VoltMod
