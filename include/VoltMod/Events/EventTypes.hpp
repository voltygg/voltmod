#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/HitGroup.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Typed views over the game events VoltMod and its plugins consume.
 *
 * Each struct names its engine event and decodes the raw fields once in From(), so handlers
 * read `e.VictimSlot` instead of `event->GetPlayerSlot("userid").Get()`. Subscribe with
 * @ref GameEvents::On:
 *
 * @code
 * _death = runtime.GameEvents.On<PlayerDeath>([](const PlayerDeath& e) {
 *     if (e.VictimSlot >= 0) ...;
 * });
 * @endcode
 *
 * Slots decode via GetPlayerSlot (userid -> slot; -1 when absent). This is the only way to
 * subscribe: there is no string form, so consuming a new event means adding its struct here and
 * decoding every field it needs in one place.
 */

struct PlayerDeath
{
    static constexpr std::string_view Name = "player_death";
    int VictimSlot = -1;
    int AttackerSlot = -1;
    std::string Weapon;
    bool Headshot = false;
    /** Number of surfaces the killing bullet went through; > 0 means a wallbang. */
    int Penetrated = 0;
    static PlayerDeath From(IGameEvent& e);
};

struct PlayerSpawn
{
    static constexpr std::string_view Name = "player_spawn";
    int Slot = -1;
    static PlayerSpawn From(IGameEvent& e);
};

struct PlayerJump
{
    static constexpr std::string_view Name = "player_jump";
    int Slot = -1;
    static PlayerJump From(IGameEvent& e);
};

struct PlayerHurt
{
    static constexpr std::string_view Name = "player_hurt";
    int VictimSlot = -1;
    int AttackerSlot = -1;
    int Health = 0;
    int DamageHealth = 0;
    /** Where the shot landed. Unlike @ref DamageView::Hitbox this comes from the event, so it is
     *  @ref HitGroup::Generic - not Invalid - for damage that carries no hitgroup. */
    HitGroup Hitbox = HitGroup::Generic;
    static PlayerHurt From(IGameEvent& e);
};

struct PlayerBlind
{
    static constexpr std::string_view Name = "player_blind";
    int Slot = -1;
    int AttackerSlot = -1;
    float BlindDuration = 0.0f;
    static PlayerBlind From(IGameEvent& e);
};

struct PlayerTeam
{
    static constexpr std::string_view Name = "player_team";
    int Slot = -1;
    int Team = 0;
    int OldTeam = 0;
    bool Disconnect = false;
    static PlayerTeam From(IGameEvent& e);
};

struct PlayerConnectFull
{
    static constexpr std::string_view Name = "player_connect_full";
    int Slot = -1;
    static PlayerConnectFull From(IGameEvent& e);
};

struct WeaponFire
{
    static constexpr std::string_view Name = "weapon_fire";
    int Slot = -1;
    std::string Weapon;
    static WeaponFire From(IGameEvent& e);
};

/**
 * @brief One bullet landing, fired once per impact (so a shotgun blast produces several).
 *
 * The engine truncates this event's `userid` to its low byte, so it does not round-trip to a slot:
 * @ref Slot is a best-effort decode, -1 whenever the truncated value names no live player.
 * Correlate impacts by tick proximity (with @ref TruncatedUserId as a disambiguator), never by
 * identity alone.
 */
struct BulletImpact
{
    static constexpr std::string_view Name = "bullet_impact";
    int Slot = -1;
    /** Raw `userid` as the engine sent it - the shooter's userid masked to its low byte. */
    int TruncatedUserId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    static BulletImpact From(IGameEvent& e);
};

struct RoundStart
{
    static constexpr std::string_view Name = "round_start";
    static RoundStart From(IGameEvent& e);
};

struct RoundEnd
{
    static constexpr std::string_view Name = "round_end";
    int Winner = 0;
    int Reason = 0;
    static RoundEnd From(IGameEvent& e);
};

struct RoundPrestart
{
    static constexpr std::string_view Name = "round_prestart";
    static RoundPrestart From(IGameEvent& e);
};

/** One ballot in the game's own vote panel; @ref VoltMod::Vote counts these. */
struct VoteCast
{
    static constexpr std::string_view Name = "vote_cast";
    int Slot = -1;
    /** Index into the panel's options; 0 is yes and 1 is no for the yes/no issue. */
    int Option = 0;
    static VoteCast From(IGameEvent& e);
};

}  // namespace VoltMod
