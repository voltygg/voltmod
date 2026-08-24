#pragma once

#include <array>
#include <cstdint>

namespace VoltMod::Sdk
{

/** One CSubtickMoveStep from the usercmd: a sub-tick input change with its intra-tick time. */
struct SubtickMove
{
    uint64_t Button = 0;   // button bit(s) this step toggles; 0 for pure aim steps
    bool Pressed = false;  // press vs release when Button != 0
    float When = 0.0f;     // intra-tick fraction [0,1)
    float PitchDelta = 0.0f;
    float YawDelta = 0.0f;
};

/**
 * One CSGOInputHistoryEntryPB. ViewYaw/ViewPitch is the angle the bullet was fired along, which
 * a cheat can diverge from the visible viewangles (the silent-aim signature).
 */
struct InputHistorySample
{
    bool HasViewAngles = false;
    float ViewPitch = 0.0f;       // view_angles.x
    float ViewYaw = 0.0f;         // view_angles.y
    int32_t TargetEntIndex = -1;  // entity the client claims it hit, -1 if none
};

/**
 * @brief Protobuf-free snapshot of the CUserCmd handed to
 * CPlayer_MovementServices::RunCommand, decoded by MovementHook for its
 * cmd listeners (ListenPreCmd/ListenPostCmd).
 *
 * Valid is false when the usercmd pointer was null or the "UserCmdPB" gamedata
 * offset is missing - fields then hold their defaults and must not be trusted.
 */
struct UserCmdView
{
    bool Valid = false;
    int32_t ClientTick = 0;

    // The client's own command counter: the int32 CUserCmd carries next to its payload (gamedata
    // "UserCmdNumber"), falling back to the always-zero CBaseUserCmdPB.legacy_command_number when
    // that offset is missing. A gap means commands were lost, reordered, or synthesized.
    int32_t CommandNumber = 0;

    /** False when the client omitted viewangles: the three fields below then hold defaults rather
     *  than a reading, so treat the angles as untrusted. */
    bool HasViewAngles = false;

    // CBaseUserCmdPB.viewangles (x = pitch, y = yaw, z = roll)
    float ViewPitch = 0.0f;
    float ViewYaw = 0.0f;
    float ViewRoll = 0.0f;

    float ForwardMove = 0.0f;
    float LeftMove = 0.0f;

    // CInButtonStatePB words: held and changed-this-tick button masks
    uint64_t ButtonsHeld = 0;
    uint64_t ButtonsChanged = 0;

    // Raw mouse movement the client reported for this command
    int32_t MouseDx = 0;
    int32_t MouseDy = 0;

    // Sub-tick shooting bookkeeping (-1 = no attack started this command)
    int32_t Attack1StartHistoryIndex = -1;
    int32_t Attack2StartHistoryIndex = -1;

    static constexpr int MaxSubtickMoves = 12;
    int SubtickMoveCount = 0;  // clamped to MaxSubtickMoves
    std::array<SubtickMove, MaxSubtickMoves> SubtickMoves{};

    // Per-shot input-history entries (the fired view angles), clamped to MaxInputHistory.
    // Attack1/Attack2StartHistoryIndex address the client's *full* input_history: an index at or
    // past InputHistorySampleCount means that shot's entry was capped away and is absent here.
    static constexpr int MaxInputHistory = 16;
    int InputHistorySampleCount = 0;
    /** Entries the client actually sent, before the MaxInputHistory cap. Greater than
     *  InputHistorySampleCount means the tail was capped away. */
    int InputHistoryTotalCount = 0;
    std::array<InputHistorySample, MaxInputHistory> InputHistorySamples{};

    /** The decoded entry at @p index, or nullptr when @p index is negative or was capped away. */
    const InputHistorySample* SampleAt(int index) const
    {
        if (index < 0 || index >= InputHistorySampleCount)
            return nullptr;
        return &InputHistorySamples[index];
    }
};

}  // namespace VoltMod::Sdk
