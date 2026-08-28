#include "Entities/SchemaResolve.hpp"

#include <VoltMod/Entities/Field.hpp>
#include <doctest/doctest.h>
#include <ostream>
#include <string_view>

using VoltMod::FieldKey;
using VoltMod::FieldQueryResult;
using VoltMod::FieldRef;
using VoltMod::FixedString;
using VoltMod::PendingField;
using VoltMod::ResetFieldCache;
using VoltMod::ResolveField;
using VoltMod::SchemaField;
using VoltMod::SetFieldQuery;

// The fake backend the cases drive. File-static rather than captured state because the seam is a
// plain function pointer, which is the point: it costs nothing on the real path.
static bool g_available = false;
static int g_calls = 0;

static FieldQueryResult FakeQuery(std::string_view klass, std::string_view field)
{
    ++g_calls;
    if (!g_available)
        return {};
    if (klass == "CBaseEntity" && field == "m_iHealth")
        return {.Available = true, .Found = true, .Ref = {.Offset = 0x344, .Size = 4, .Networked = true}};
    return {.Available = true, .Found = false, .Ref = {}};
}

/** Put the resolver back to a known state; each case owns the process-wide cache while it runs. */
static void Reset(bool available)
{
    g_available = available;
    g_calls = 0;
    ResetFieldCache();
    SetFieldQuery(&FakeQuery);
}

TEST_CASE("FixedString keeps the literal without its NUL")
{
    constexpr FixedString name{"m_iHealth"};
    static_assert(name.View() == "m_iHealth");
    static_assert(name.View().size() == 9);

    constexpr FixedString empty{""};
    static_assert(empty.View().empty());
}

TEST_CASE("FieldKey is stable and separates the class from the field")
{
    // Pinned so a change to the hash is a deliberate one - the key is the cache's identity.
    static_assert(FieldKey("CBaseEntity", "m_iHealth") == 0xacfbf0482d98a239ULL);
    static_assert(FieldKey("CBaseEntity", "m_iTeamNum") == 0x214f9e1b00d1abb6ULL);

    // The separator is what keeps these apart; a plain concatenation would collide.
    CHECK(FieldKey("CBase", "Entitym_iHealth") != FieldKey("CBaseEntity", "m_iHealth"));
    CHECK(FieldKey("CBaseEntity", "m_iHealth") != FieldKey("CBaseEntity", "m_iTeamNum"));
    CHECK(FieldKey("CCSPlayerPawn", "m_iHealth") != FieldKey("CBaseEntity", "m_iHealth"));
}

TEST_CASE("ResolveField answers pending and caches nothing before the schema system is live")
{
    Reset(false);

    const FieldRef& first = ResolveField("CBaseEntity", "m_iHealth", 4);
    CHECK(&first == &PendingField());
    CHECK_FALSE(static_cast<bool>(first));
    CHECK(g_calls == 1);

    // Nothing was cached, so the next ask reaches the backend again rather than repeating "no".
    const FieldRef& second = ResolveField("CBaseEntity", "m_iHealth", 4);
    CHECK(&second == &PendingField());
    CHECK(g_calls == 2);

    // ...and once the engine hands the schema system over, the same lookup resolves.
    g_available = true;
    const FieldRef& third = ResolveField("CBaseEntity", "m_iHealth", 4);
    CHECK(&third != &PendingField());
    CHECK(third.Offset == 0x344);
    CHECK(third.Networked);

    Reset(false);
}

TEST_CASE("ResolveField caches hits and misses once the schema system is live")
{
    Reset(true);

    const FieldRef& hit = ResolveField("CBaseEntity", "m_iHealth", 4);
    CHECK(hit.Offset == 0x344);
    CHECK(g_calls == 1);

    const FieldRef& again = ResolveField("CBaseEntity", "m_iHealth", 4);
    CHECK(&again == &hit);  // the same cache entry, and stable enough to hold on to
    CHECK(g_calls == 1);

    const FieldRef& miss = ResolveField("CBaseEntity", "m_notAField", 4);
    CHECK_FALSE(static_cast<bool>(miss));
    CHECK(&miss != &PendingField());
    CHECK(g_calls == 2);

    // A miss is cached like a hit, so a wrong name costs one walk rather than one per call.
    ResolveField("CBaseEntity", "m_notAField", 4);
    CHECK(g_calls == 2);

    Reset(false);
}

TEST_CASE("SchemaField retries while the schema system is pending and settles once it answers")
{
    Reset(false);

    const SchemaField<int32_t> health{"CBaseEntity", "m_iHealth"};
    CHECK_FALSE(static_cast<bool>(health));
    CHECK(health->Offset == -1);

    g_available = true;
    CHECK(static_cast<bool>(health));
    CHECK(health->Offset == 0x344);

    const int callsAfterResolve = g_calls;
    CHECK(health->Offset == 0x344);
    CHECK(g_calls == callsAfterResolve);  // settled: no further backend calls

    Reset(false);
}
