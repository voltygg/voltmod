#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <algorithm>
#include <doctest/doctest.h>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

using VoltMod::Bindings;
using VoltMod::ErrorCode;
using VoltMod::GameDataLocation;
using VoltMod::GameDataSection;

/** A lookup that binds every key but the ones it was told to refuse, and remembers what was asked. */
class FakeGameData
{
public:
    /** Refuse @p key with @p reason instead of binding it. */
    void Refuse(std::string key, std::string reason) { _refused.emplace(std::move(key), std::move(reason)); }

    /** What @p key was looked up as, or no section at all when nothing looked it up. */
    GameDataSection AskedFor(std::string_view key) const
    {
        const auto it = _asked.find(std::string(key));
        return it != _asked.end() ? it->second : GameDataSection{};
    }

    GameDataLocation operator()(GameDataSection sections, std::string_view name)
    {
        const std::string key(name);
        _asked[key] = sections;

        const auto refused = _refused.find(key);
        if (refused == _refused.end())
            return {.Found = true, .Address = &_anything, .Value = 8};

        _reason = refused->second;
        return {.Reason = _reason};
    }

private:
    std::map<std::string, std::string> _refused;
    std::map<std::string, GameDataSection> _asked;
    std::string _reason;  ///< borrowed by the last lookup, like the host's own
    int _anything = 0;    ///< stands in for any address the host resolved
};

static bool HasFailure(const Bindings& bindings, std::string_view failure)
{
    return std::ranges::any_of(bindings.Failures, [&](const std::string& each) { return each == failure; });
}

TEST_CASE("Bind takes every member from the lookup and reports nothing")
{
    FakeGameData gameData;
    Bindings bindings;

    CHECK(bindings.Bind(std::ref(gameData)).has_value());

    CHECK(bindings.Failures.empty());
    CHECK(static_cast<bool>(bindings.CreateEntityByName));
    CHECK(static_cast<bool>(bindings.EmitSoundFilter));
    CHECK(static_cast<bool>(bindings.Teleport));
    CHECK(bindings.Teleport.Index() == 8);
    CHECK(bindings.GameEntitySystem.Value() == 8);
}

TEST_CASE("Each member asks for the sections its key may live in")
{
    FakeGameData gameData;
    Bindings bindings;
    CHECK(bindings.Bind(std::ref(gameData)).has_value());

    CHECK(gameData.AskedFor("CreateEntityByName") == GameDataSection::Function);
    CHECK(gameData.AskedFor("CBaseEntity::EmitSoundFilter") == (GameDataSection::Function | GameDataSection::Global));
    CHECK(gameData.AskedFor("CBaseEntity::Teleport") == GameDataSection::VTable);
    CHECK(gameData.AskedFor("CBaseEntity::TakeDamageOld") == GameDataSection::Function);
    CHECK(gameData.AskedFor("CheckTransmitPlayerSlot") == GameDataSection::Offset);
}

TEST_CASE("A member the lookup cannot bind keeps its reason and stays unbound")
{
    FakeGameData gameData;
    gameData.Refuse("CheckTransmitPlayerSlot", "not in gamedata");
    gameData.Refuse("CBaseEntity::Teleport", "index -1 is negative");

    Bindings bindings;
    const auto bound = bindings.Bind(std::ref(gameData));

    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().Code == ErrorCode::Engine);
    CHECK(bound.error().Detail.starts_with("2 did not bind: "));
    CHECK(HasFailure(bindings, "CheckTransmitPlayerSlot: not in gamedata"));
    CHECK(HasFailure(bindings, "CBaseEntity::Teleport: index -1 is negative"));
    CHECK_FALSE(static_cast<bool>(bindings.VisibilityRecipientSlot));
    CHECK(bindings.VisibilityRecipientSlot.Value() == -1);
    CHECK_FALSE(static_cast<bool>(bindings.Teleport));
    CHECK(bindings.Teleport.Index() == -1);
    // A member the lookup did bind is unaffected.
    CHECK(static_cast<bool>(bindings.CreateEntityByName));
}

TEST_CASE("Binding again clears what the last one left")
{
    FakeGameData refusing;
    refusing.Refuse("CreateEntityByName", "pattern not found");

    Bindings bindings;
    CHECK_FALSE(bindings.Bind(std::ref(refusing)).has_value());
    REQUIRE(bindings.Failures.size() == 1);

    FakeGameData binding;
    CHECK(bindings.Bind(std::ref(binding)).has_value());
    CHECK(bindings.Failures.empty());
    CHECK(static_cast<bool>(bindings.CreateEntityByName));
}
