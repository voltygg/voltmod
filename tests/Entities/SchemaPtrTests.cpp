#include "Entities/SchemaResolve.hpp"

#include <VoltMod/Entities/SchemaPtr.hpp>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <string_view>

using VoltMod::FieldQueryResult;
using VoltMod::ResetFieldCache;
using VoltMod::SchemaField;
using VoltMod::SchemaPtr;
using VoltMod::SetFieldQuery;

// Stand-ins for an engine object: an outer "entity" holding one sub-object behind a pointer and
// one embedded inline, which are exactly the two shapes Follow and At exist to reach.
struct FakeServices
{
    int32_t Account = 0;
    int32_t Counts[4] = {0, 0, 0, 0};
};

struct FakeEmbedded
{
    uint8_t Enabled = 0;
};

struct FakeEntity
{
    int32_t Health = 0;
    FakeServices* Money = nullptr;
    FakeEmbedded State;
};

/** Answers with the real offsets of the structs above, keyed the way the schema keys them. */
static FieldQueryResult FakeQuery(std::string_view klass, std::string_view field)
{
    auto hit = [](size_t offset, size_t size) {
        return FieldQueryResult{.Available = true,
                                .Found = true,
                                .Ref = {.Offset = static_cast<int32_t>(offset), .Size = static_cast<int32_t>(size)}};
    };

    if (klass == "Entity" && field == "m_iHealth")
        return hit(offsetof(FakeEntity, Health), sizeof(int32_t));
    if (klass == "Entity" && field == "m_pMoney")
        return hit(offsetof(FakeEntity, Money), sizeof(void*));
    if (klass == "Entity" && field == "m_state")
        return hit(offsetof(FakeEntity, State), sizeof(FakeEmbedded));
    if (klass == "Services" && field == "m_iAccount")
        return hit(offsetof(FakeServices, Account), sizeof(int32_t));
    if (klass == "Services" && field == "m_nCounts")
        return hit(offsetof(FakeServices, Counts), sizeof(FakeServices::Counts));
    if (klass == "Embedded" && field == "m_bEnabled")
        return hit(offsetof(FakeEmbedded, Enabled), sizeof(uint8_t));

    return {.Available = true, .Found = false, .Ref = {}};
}

// The offsets are declared per case rather than file-static: ResetFieldCache drops the entries a
// SchemaField remembers, so one that outlived a case would hold a pointer into the old cache.
#define SCHEMA_PTR_FIXTURE                                         \
    ResetFieldCache();                                             \
    SetFieldQuery(&FakeQuery);                                     \
    const SchemaField<int32_t> kHealth{"Entity", "m_iHealth"};     \
    const SchemaField<void*> kMoney{"Entity", "m_pMoney"};         \
    const SchemaField<void> kState{"Entity", "m_state"};           \
    const SchemaField<int32_t> kAccount{"Services", "m_iAccount"}; \
    const SchemaField<int32_t[]> kCounts{"Services", "m_nCounts"}; \
    const SchemaField<uint8_t> kEnabled{"Embedded", "m_bEnabled"}; \
    const SchemaField<int32_t> kMissing{"Entity", "m_notAField"}

TEST_CASE("A null SchemaPtr reads a fallback and refuses to write")
{
    SCHEMA_PTR_FIXTURE;

    const SchemaPtr none;
    CHECK_FALSE(static_cast<bool>(none));
    CHECK(none.Raw() == nullptr);
    CHECK(none.Get(kHealth) == 0);
    CHECK(none.Get(kHealth, -1) == -1);
    CHECK(none.Ptr(kHealth) == nullptr);
    CHECK_FALSE(none.Set(kHealth, 100));

    ResetFieldCache();
}

TEST_CASE("An unresolved field reads the fallback rather than whatever sits at offset zero")
{
    SCHEMA_PTR_FIXTURE;

    FakeEntity entity{.Health = 42};
    const SchemaPtr live{&entity};

    CHECK(static_cast<bool>(live));
    CHECK(live.Get(kMissing, -1) == -1);
    CHECK_FALSE(live.Set(kMissing, 7));
    CHECK(entity.Health == 42);  // the miss wrote nothing at all, least of all offset 0

    ResetFieldCache();
}

TEST_CASE("Get and Set reach a field on the object itself")
{
    SCHEMA_PTR_FIXTURE;

    FakeEntity entity{.Health = 42};
    const SchemaPtr live{&entity};

    CHECK(live.Get(kHealth) == 42);
    CHECK(live.Set(kHealth, 100));
    CHECK(entity.Health == 100);

    ResetFieldCache();
}

TEST_CASE("Follow traverses a pointer field and null-checks the whole chain")
{
    SCHEMA_PTR_FIXTURE;

    FakeServices services{.Account = 16000};
    FakeEntity entity{.Money = &services};
    const SchemaPtr live{&entity};

    CHECK(live.Follow(kMoney).Get(kAccount) == 16000);
    CHECK(live.Follow(kMoney).Set(kAccount, 800));
    CHECK(services.Account == 800);

    // A null pointer field ends the chain rather than dereferencing it.
    entity.Money = nullptr;
    CHECK_FALSE(static_cast<bool>(live.Follow(kMoney)));
    CHECK(live.Follow(kMoney).Get(kAccount, -1) == -1);

    // ...and so does an unresolved one, without ever reading the object.
    CHECK_FALSE(static_cast<bool>(live.At(kMissing)));

    ResetFieldCache();
}

TEST_CASE("At reaches an embedded sub-object without dereferencing it")
{
    SCHEMA_PTR_FIXTURE;

    FakeEntity entity;
    entity.State.Enabled = 1;
    const SchemaPtr live{&entity};

    CHECK(live.At(kState).Get(kEnabled) == 1);
    CHECK(live.At(kState).Set(kEnabled, 0));
    CHECK(entity.State.Enabled == 0);

    ResetFieldCache();
}

TEST_CASE("Ptr hands back the field itself so an array field can be indexed")
{
    SCHEMA_PTR_FIXTURE;

    FakeServices services;
    FakeEntity entity{.Money = &services};
    const SchemaPtr live{&entity};

    int32_t* counts = live.Follow(kMoney).Ptr(kCounts);
    REQUIRE(counts != nullptr);
    counts[2] = 5;
    CHECK(services.Counts[2] == 5);
    CHECK(counts == services.Counts);

    ResetFieldCache();
}

TEST_CASE("Two SchemaPtrs compare equal when they name the same object")
{
    SCHEMA_PTR_FIXTURE;

    FakeServices services;
    FakeEntity entity{.Money = &services};
    const SchemaPtr live{&entity};

    CHECK(live.Follow(kMoney) == SchemaPtr{&services});
    CHECK(live != SchemaPtr{&services});
    CHECK(SchemaPtr{} == SchemaPtr{});

    ResetFieldCache();
}
