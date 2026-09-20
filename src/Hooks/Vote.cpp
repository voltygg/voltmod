#include "Engine/Net/NetMessage.hpp"
#include "Engine/Net/ProtoReflect.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Net/RecipientFilter.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Vote.hpp>
#include <engine/igameeventsystem.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string_view>

namespace VoltMod
{

static constexpr std::string_view ControllerClass = "vote_controller";

// A client takes F1/F2 only while an issue is active. The engine must never run this one, so
// `vote` and `callvote` are blocked during a vote.
static constexpr int YesNoIssueIndex = 2;
static constexpr int NoIssue = -1;
static constexpr int AllTeams = -1;

// The vote_cast event's option numbers.
static constexpr int YesOption = 0;
static constexpr int NoOption = 1;

// Names carry the message prefix: a bare "VoteFailed" also matches CCSUsrMsg_CallVoteFailed.
static constexpr std::string_view VoteStartMessage = "CCSUsrMsg_VoteStart";
static constexpr std::string_view VotePassMessage = "CCSUsrMsg_VotePass";
static constexpr std::string_view VoteFailedMessage = "CCSUsrMsg_VoteFailed";

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

bool Vote::StartVote(std::string_view title, std::string_view detail, float durationSec, int callerSlot,
                     ResultFn onResult, FinishedFn onFinished)
{
    if (_inProgress || !onResult)
        return false;

    _eligible = 0;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_entities.IsPlayerSlotValid(slot))
            ++_eligible;
    }
    if (_eligible <= 0)
        return false;

    _yes = 0;
    _no = 0;
    _voted.fill(false);

    // A new map is a new controller entity.
    _controller = Schema::CVoteController{_entities.FindByClassName({}, ControllerClass).Raw()};
    if (_controller)
    {
        _controller.SetPotentialVotes(_eligible);
        _controller.SetIsYesNoVote(true);
        // The VoteStart recipients decide who may vote.
        _controller.SetOnlyTeamToVote(AllTeams);
        _controller.SetActiveIssueIndex(YesNoIssueIndex);
    }

    _inProgress = true;
    _title = title;
    _detail = detail;
    _callerSlot = callerSlot;
    _onResult = std::move(onResult);
    _onFinished = std::move(onFinished);

    PublishCounts();
    SendVoteStart();

    // A timeout may only end the vote that scheduled it.
    const uint64_t voteId = ++_voteId;
    _timeout = _scheduler.Delay(static_cast<int64_t>(durationSec * 1000.0f), [this, voteId] {
        if (_inProgress && voteId == _voteId)
            FinishVote(VoteEndReason::TimeUp);
    });

    return true;
}

bool Vote::TryCastBallot(int slot, std::string_view option)
{
    if (!_inProgress)
        return false;

    if (!IsValidSlot(slot) || !_entities.IsPlayerSlotValid(slot) || _voted[slot])
        return true;

    if (option == "option1")
    {
        ++_yes;
        PublishBallot(slot, YesOption);
    }
    else if (option == "option2")
    {
        ++_no;
        PublishBallot(slot, NoOption);
    }
    else
    {
        return true;
    }

    _voted[slot] = true;
    PublishCounts();

    if (_yes + _no >= _eligible)
    {
        // Deferred a tick: the engine is still inside the command dispatch.
        const uint64_t voteId = _voteId;
        _deferredClose = _scheduler.NextTick([this, voteId] {
            if (_inProgress && voteId == _voteId)
                FinishVote(VoteEndReason::AllVoted);
        });
    }
    return true;
}

void Vote::EndVote(VoteEndReason reason)
{
    if (_inProgress)
        FinishVote(reason);
}

void Vote::FinishVote(VoteEndReason reason)
{
    _inProgress = false;
    ++_voteId;

    VoteTally tally{.Eligible = _eligible, .Yes = _yes, .No = _no};

    // A cancelled vote never asks the caller whether it passed.
    bool passed = reason != VoteEndReason::Cancelled && _onResult && _onResult(tally);

    SendVoteOutcome(passed);

    if (_controller)
        _controller.SetActiveIssueIndex(NoIssue);
    _controller = {};

    auto finished = std::move(_onFinished);
    _onResult = nullptr;
    _onFinished = nullptr;
    if (finished)
        finished(passed, reason);
}

void Vote::PublishBallot(int slot, int option)
{
    // The voter's panel registers the key press from this event.
    IGameEvent* event = _events.CreateEvent("vote_cast");
    if (!event)
        return;

    event->SetInt("vote_option", option);
    event->SetInt("team", AllTeams);
    event->SetPlayer("userid", CPlayerSlot(slot));
    _events.FireEvent(event);
}

void Vote::PublishCounts()
{
    // The panel reads its tally from this event.
    IGameEvent* event = _events.CreateEvent("vote_changed");
    if (!event)
        return;

    event->SetInt("vote_option1", _yes);
    event->SetInt("vote_option2", _no);
    event->SetInt("vote_option3", 0);
    event->SetInt("vote_option4", 0);
    event->SetInt("vote_option5", 0);
    event->SetInt("potentialVotes", _eligible);
    _events.FireEvent(event);
}

void Vote::SendVoteStart()
{
    MultiRecipientFilter filter = Recipients();
    PostUserMessage(_interfaces, _voteStartInternal, VoteStartMessage, filter, [this](CNetMessage* raw) {
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

    PostUserMessage(_interfaces, cached, passed ? VotePassMessage : VoteFailedMessage, filter,
                    [this, passed](CNetMessage* raw) {
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
