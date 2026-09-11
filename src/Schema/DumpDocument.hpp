#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <schemasystem/schematypes.h>
#include <string>
#include <vector>

// Schema IR consumed by `voltmod schemagen`. DumpDescribe.cpp fills it; Dump.cpp writes it.

namespace VoltMod::Schema
{

/**
 * A field type as reported by the schema.
 *
 * `atomic` distinguishes CUtlVector collections from plain atomic types. Their element type is
 * stored in `inner`.
 */
struct TypeInfo
{
    std::string name;
    std::string category;
    std::optional<std::string> atomic;
    std::optional<std::string> inner;
    std::optional<int32_t> extent;
    std::optional<std::string> declared;
};

struct FieldInfo
{
    std::string name;
    int32_t offset = 0;
    int32_t size = 0;
    TypeInfo type;
};

struct BaseInfo
{
    std::string name;
    int32_t offset = 0;
};

struct ClassInfo
{
    int32_t size = 0;
    std::vector<BaseInfo> bases;
    int32_t chain_offset = -1;
    std::vector<FieldInfo> fields;
};

struct EnumItem
{
    std::string name;
    int64_t value = 0;
};

struct EnumInfo
{
    int32_t size = 0;
    std::vector<EnumItem> items;
};

/** Ordered maps keep generated output stable. */
struct SchemaDoc
{
    /** steam.inf's ServerVersion used to identify the generated build. */
    std::string build;
    std::vector<std::string> scopes;
    std::map<std::string, ClassInfo> classes;
    std::map<std::string, EnumInfo> enums;
};

/** Describe @p klass as IR. */
ClassInfo DescribeClass(const CSchemaClassInfo* klass);

/** Describe @p enumeration as IR. */
EnumInfo DescribeEnum(const CSchemaEnumInfo* enumeration);

}  // namespace VoltMod::Schema
