#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <cstdint>
#include <functional>
#include <string>

namespace VoltMod
{

/** Why a vote stopped taking ballots. */
enum class VoteEndReason
{
    AllVoted,  ///< every eligible player cast a ballot
    TimeUp,
    Cancelled,  ///< an admin (or a peer feature) called it off
};

/** The tally handed to the result callback. */
struct VoteTally
{
    int Eligible = 0;  ///< players who could vote
    int Yes = 0;
    int No = 0;

    int Cast() const { return Yes + No; }
};

/**
 * @brief The game's own yes/no vote panel, driven through the map's `vote_controller`.
 *
 * A vote configures the controller entity, broadcasts a `VoteStart` user message, counts the
 * `vote_cast` game events that come back, and finishes with `VotePass` or `VoteFailed`. Only one
 * vote runs at a time; StartVote() refuses while one is live.
 *
 * The panel is the engine's, so its title must be a localization token the client already has -
 * a `#SFUI_vote...` or `#Panorama_vote...` string. Arbitrary text does not render.
 *
 * Every callback runs on the game thread.
 */
class Vote
{
public:
    /** Decides whether the vote passed. Called once when voting ends. */
    using ResultFn = std::function<bool(const VoteTally&)>;
    /** Called after the pass/fail panel is sent, with what the result callback decided. */
    using FinishedFn = std::function<void(bool passed, VoteEndReason reason)>;

    /** All five must outlive this service; the Runtime declares them above it. */
    Vote(Interfaces& interfaces, EntitySystem& entities, SchemaService& schema, GameEvents& events,
         Scheduler& scheduler);
    Vote(const Vote&) = delete;
    Vote& operator=(const Vote&) = delete;

    /**
     * Open a yes/no vote for every connected human.
     *
     * Subscribes to `vote_cast` on first use, so there is no separate arming step to forget.
     *
     * @param title a `#SFUI_vote` / `#Panorama_vote` localization token; see the class docs.
     * @param detail the token's detail string, often a map or player name.
     * @param durationSec how long before the vote closes itself.
     * @param callerSlot whose name the panel credits; -1 for the server.
     * @return false when a vote is already running or the map has no vote controller.
     */
    bool StartVote(const std::string& title, const std::string& detail, float durationSec, int callerSlot,
                   ResultFn onResult, FinishedFn onFinished = {});

    /** End the running vote early. No-op when none is running. */
    void EndVote(VoteEndReason reason);

    bool InProgress() const { return _inProgress; }

private:
    void OnVoteCast(int slot, int option);
    void FinishVote(VoteEndReason reason);
    void SendVoteStart();
    void SendVoteOutcome(bool passed);
    void PublishCounts();
    /** Find the map's vote_controller and cache its ballot offsets. False when the map has none. */
    bool AcquireController();
    /** Every connected slot - who a vote panel is sent to. */
    MultiRecipientFilter Recipients() const;

    int OptionCount(int option) const;
    void SetOptionCount(int option, int value);
    void ResetBallots();

    Interfaces& _interfaces;
    EntitySystem& _entities;
    SchemaService& _schema;
    GameEvents& _events;
    Scheduler& _scheduler;

    Subscription _voteCastSub;
    /** Resolved message types, cached on first send; they are stable for the process. */
    INetworkMessageInternal* _voteStartInternal = nullptr;
    INetworkMessageInternal* _votePassInternal = nullptr;
    INetworkMessageInternal* _voteFailedInternal = nullptr;
    void* _controller = nullptr;
    /** Ballot-array offsets on `_controller`, resolved with it: they are read once per ballot. */
    int _offsetOptionCount = -1;
    int _offsetVotesCast = -1;
    bool _inProgress = false;
    /** Bumped per vote so a timeout cannot end the vote that replaced it. */
    uint64_t _voteId = 0;
    int _eligible = 0;
    int _callerSlot = -1;
    std::string _title;
    std::string _detail;
    ResultFn _onResult;
    FinishedFn _onFinished;
};

}  // namespace VoltMod
