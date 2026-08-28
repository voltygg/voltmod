#include "Engine/NetMessage.hpp"
#include "Engine/ProtoReflect.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/EventTypes.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Vote.hpp>
#include <engine/igameeventsystem.h>
#include <format>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string_view>

namespace VoltMod
{

static constexpr std::string_view ControllerClass = "vote_controller";
static constexpr std::string_view VoteControllerSchema = "CVoteController";

// The engine's yes/no issue index; option 0 is Yes, option 1 is No, 3 means "not voted".
static constexpr int YesNoIssueIndex = 2;
static constexpr int OptionYes = 0;
static constexpr int OptionNo = 1;
static constexpr int VoteUncast = 3;
static constexpr int OptionSlots = 5;
static constexpr int AllTeams = -1;

static const SchemaField<int[]> kOptionCount{VoteControllerSchema, "m_nVoteOptionCount"};
static const SchemaField<int[]> kVotesCast{VoteControllerSchema, "m_nVotesCast"};
static const SchemaField<int> kPotentialVotes{VoteControllerSchema, "m_nPotentialVotes"};
static const SchemaField<bool> kIsYesNoVote{VoteControllerSchema, "m_bIsYesNoVote"};
static const SchemaField<int> kActiveIssueIndex{VoteControllerSchema, "m_iActiveIssueIndex"};
static const SchemaField<int> kOnlyTeamToVote{VoteControllerSchema, "m_iOnlyTeamToVote"};

static ProtoMessage* AsProto(CNetMessage* message)
{
    return message ? message->ToPB<ProtoMessage>() : nullptr;
}

/** @ref ProtoField plus what a missing field costs here; see Engine/ProtoReflect.hpp for why the
 *  fields are reached by name at all. */
static const ProtoFieldDescriptor* VoteField(ProtoMessage* message, std::string_view name)
{
    const auto* field = ProtoField(*message, name);
    if (!field)
    {
        const auto* descriptor = message->GetDescriptor();
        Log::Warn("Vote: {} has no field '{}'; the panel may render incomplete.",
                  descriptor ? descriptor->name() : "<unknown>", name);
    }
    return field;
}

static void SetInt(ProtoMessage* message, std::string_view name, int32_t value)
{
    if (const auto* field = VoteField(message, name))
        message->GetReflection()->SetInt32(message, field, value);
}

static void SetBool(ProtoMessage* message, std::string_view name, bool value)
{
    if (const auto* field = VoteField(message, name))
        message->GetReflection()->SetBool(message, field, value);
}

static void SetString(ProtoMessage* message, std::string_view name, const std::string& value)
{
    if (const auto* field = VoteField(message, name))
        message->GetReflection()->SetString(message, field, value);
}

Vote::Vote(Interfaces& interfaces, EntitySystem& entities, GameEvents& events, Scheduler& scheduler)
    : _interfaces(interfaces), _entities(entities), _events(events), _scheduler(scheduler)
{}

bool Vote::AcquireController()
{
    // The controller is a map entity, so it is a different object after every map change.
    _controller = SchemaPtr{_entities.FindByClassName({}, ControllerClass).Raw()};
    return static_cast<bool>(_controller);
}

MultiRecipientFilter Vote::Recipients() const
{
    MultiRecipientFilter filter;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (const_cast<EntitySystem&>(_entities).IsPlayerSlotValid(slot))
            filter.AddRecipient(slot);
    }
    return filter;
}

// m_nVoteOptionCount and m_nVotesCast are both int arrays on the controller, so the field resolves
// to the array head and the caller indexes it.
int Vote::OptionCount(int option) const
{
    const int* counts = _controller.Ptr(kOptionCount);
    if (!counts || option < 0 || option >= OptionSlots)
        return 0;
    return counts[option];
}

void Vote::SetOptionCount(int option, int value)
{
    int* counts = _controller.Ptr(kOptionCount);
    if (!counts || option < 0 || option >= OptionSlots)
        return;
    counts[option] = value;
}

void Vote::ResetBallots()
{
    for (int option = 0; option < OptionSlots; ++option)
        SetOptionCount(option, 0);

    int* ballots = _controller.Ptr(kVotesCast);
    if (!ballots)
        return;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        ballots[slot] = VoteUncast;
}

bool Vote::StartVote(std::string_view title, std::string_view detail, float durationSec, int callerSlot,
                     ResultFn onResult, FinishedFn onFinished)
{
    if (_inProgress || !onResult)
        return false;

    // Subscribed here rather than in a separate arming call: a vote that counted no ballots
    // because nobody armed the service is a silent failure with no good diagnostic.
    if (!_voteCastSub)
    {
        _voteCastSub = _events.On<VoteCast>([this](const VoteCast& e) {
            if (_inProgress)
                OnVoteCast(e.Slot, e.Option);
        });
    }

    if (!AcquireController())
    {
        Log::Warn("Vote: this map has no vote_controller; no vote can be shown.");
        return false;
    }

    _eligible = 0;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_entities.IsPlayerSlotValid(slot))
            ++_eligible;
    }
    if (_eligible <= 0)
        return false;

    ResetBallots();

    _controller.Set(kPotentialVotes, _eligible);
    _controller.Set(kIsYesNoVote, true);
    _controller.Set(kActiveIssueIndex, YesNoIssueIndex);
    // Who may vote is decided by the recipients of the VoteStart message, not by this field.
    _controller.Set(kOnlyTeamToVote, AllTeams);

    _inProgress = true;
    _title = title;
    _detail = detail;
    _callerSlot = callerSlot;
    _onResult = std::move(onResult);
    _onFinished = std::move(onFinished);

    PublishCounts();
    SendVoteStart();

    // Captured by value so a timeout can only ever end the vote that scheduled it; a vote that
    // finished early has already moved the id on.
    const uint64_t voteId = ++_voteId;
    _timeout = _scheduler.Delay(static_cast<int64_t>(durationSec * 1000.0f), [this, voteId] {
        if (_inProgress && voteId == _voteId)
            FinishVote(VoteEndReason::TimeUp);
    });

    return true;
}

void Vote::OnVoteCast(int slot, int option)
{
    if (slot < 0 || option < 0 || option >= OptionSlots)
        return;

    PublishCounts();

    // Close as soon as everyone who could vote has, rather than sitting on a decided vote.
    if (OptionCount(OptionYes) + OptionCount(OptionNo) >= _eligible)
    {
        // Deferred a tick: ending inside the event dispatch that produced the last ballot tears
        // down state the engine is still walking.
        const uint64_t voteId = _voteId;
        _deferredClose = _scheduler.NextTick([this, voteId] {
            if (_inProgress && voteId == _voteId)
                FinishVote(VoteEndReason::AllVoted);
        });
    }
}

void Vote::EndVote(VoteEndReason reason)
{
    if (_inProgress)
        FinishVote(reason);
}

void Vote::FinishVote(VoteEndReason reason)
{
    _inProgress = false;
    ++_voteId;  // any timeout still pending for this vote is now stale

    VoteTally tally{.Eligible = _eligible, .Yes = OptionCount(OptionYes), .No = OptionCount(OptionNo)};

    // A cancelled vote never asks the caller whether it passed.
    bool passed = reason != VoteEndReason::Cancelled && _onResult && _onResult(tally);

    SendVoteOutcome(passed);

    _controller.Set(kActiveIssueIndex, -1);
    _controller = {};

    auto finished = std::move(_onFinished);
    _onResult = nullptr;
    _onFinished = nullptr;
    if (finished)
        finished(passed, reason);
}

void Vote::PublishCounts()
{
    // The panel reads its running tally from vote_changed, not from the entity, so the counts
    // have to be re-announced after every ballot.
    IGameEvent* event = _events.CreateEvent("vote_changed");
    if (!event)
        return;

    for (int option = 0; option < OptionSlots; ++option)
        event->SetInt(std::format("vote_option{}", option + 1).c_str(), OptionCount(option));
    event->SetInt("potentialVotes", _eligible);

    _events.FireEvent(event, false);
}

void Vote::SendVoteStart()
{
    MultiRecipientFilter filter = Recipients();
    PostUserMessage(_interfaces, _voteStartInternal, "VoteStart", filter, [this](CNetMessage* raw) {
        auto* start = AsProto(raw);
        if (!start)
            return false;
        SetInt(start, "team", AllTeams);
        SetInt(start, "player_slot", _callerSlot);
        SetInt(start, "vote_type", -1);
        SetString(start, "disp_str", _title);
        SetString(start, "details_str", _detail);
        SetBool(start, "is_yes_no_vote", true);
        return true;
    });
}

void Vote::SendVoteOutcome(bool passed)
{
    // Pass and fail are distinct message types, so each gets its own cache slot.
    auto& cached = passed ? _votePassInternal : _voteFailedInternal;
    MultiRecipientFilter filter = Recipients();

    PostUserMessage(_interfaces, cached, passed ? "VotePass" : "VoteFailed", filter, [this, passed](CNetMessage* raw) {
        auto* outcome = AsProto(raw);
        if (!outcome)
            return false;
        SetInt(outcome, "team", AllTeams);
        if (passed)
        {
            SetInt(outcome, "vote_type", -1);
            SetString(outcome, "disp_str", _title);
            SetString(outcome, "details_str", _detail);
        }
        else
        {
            SetInt(outcome, "reason", 0);
        }
        return true;
    });
}

}  // namespace VoltMod
