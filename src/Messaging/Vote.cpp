#include "Engine/NetMessage.hpp"
#include "Entities/Schema.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Messaging/Vote.hpp>
#include <engine/igameeventsystem.h>
#include <format>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>

namespace VoltMod
{

static constexpr const char* ControllerClass = "vote_controller";
static constexpr const char* VoteControllerSchema = "CVoteController";

// The engine's yes/no issue index; option 0 is Yes, option 1 is No, 3 means "not voted".
static constexpr int YesNoIssueIndex = 2;
static constexpr int OptionYes = 0;
static constexpr int OptionNo = 1;
static constexpr int VoteUncast = 3;
static constexpr int OptionSlots = 5;
static constexpr int AllTeams = -1;

using ProtoMessage = google::protobuf::Message;

/**
 * The CS-specific user messages (CCSUsrMsg_VoteStart and friends) are declared in the SDK's
 * cstrike15_usermessages.proto but are not generated into headers, and consumer builds
 * deliberately do not run protoc. The engine has already registered their descriptors though,
 * so the fields are set through protobuf reflection by name instead - no generated type, no
 * build-system change, and a renamed field degrades to a warning rather than miscompiling.
 */
static ProtoMessage* AsProto(CNetMessage* message)
{
    return message ? message->ToPB<ProtoMessage>() : nullptr;
}

static const google::protobuf::FieldDescriptor* Field(ProtoMessage* message, const char* name)
{
    const auto* descriptor = message->GetDescriptor();
    const auto* field = descriptor ? descriptor->FindFieldByName(name) : nullptr;
    if (!field)
        Log::Warn("Vote: {} has no field '{}'; the panel may render incomplete.",
                  descriptor ? descriptor->name() : "<unknown>", name);
    return field;
}

static void SetInt(ProtoMessage* message, const char* name, int32_t value)
{
    if (const auto* field = Field(message, name))
        message->GetReflection()->SetInt32(message, field, value);
}

static void SetBool(ProtoMessage* message, const char* name, bool value)
{
    if (const auto* field = Field(message, name))
        message->GetReflection()->SetBool(message, field, value);
}

static void SetString(ProtoMessage* message, const char* name, const std::string& value)
{
    if (const auto* field = Field(message, name))
        message->GetReflection()->SetString(message, field, value);
}

Vote::Vote(Interfaces& interfaces, EntitySystem& entities, SchemaService& schema, GameEvents& events,
           Scheduler& scheduler)
    : _interfaces(interfaces), _entities(entities), _schema(schema), _events(events), _scheduler(scheduler)
{}

bool Vote::AcquireController()
{
    // The controller is a map entity, so it is a different object after every map change; its
    // ballot-array offsets are read once per ballot, so they are cached with it rather than
    // re-resolved through the schema maps on every access.
    _controller = _entities.FindByClassName(nullptr, ControllerClass);
    if (!_controller)
        return false;

    _offsetOptionCount = _schema.GetOffset(VoteControllerSchema, "m_nVoteOptionCount");
    _offsetVotesCast = _schema.GetOffset(VoteControllerSchema, "m_nVotesCast");
    return true;
}

MultiRecipientFilter Vote::Recipients() const
{
    MultiRecipientFilter filter;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (const_cast<EntitySystem&>(_entities).GetPlayerController(slot))
            filter.AddRecipient(slot);
    }
    return filter;
}

int Vote::OptionCount(int option) const
{
    if (!_controller || _offsetOptionCount < 0 || option < 0 || option >= OptionSlots)
        return 0;
    return ReadAt<int>(_controller, _offsetOptionCount + option * static_cast<int>(sizeof(int)));
}

void Vote::SetOptionCount(int option, int value)
{
    if (!_controller || _offsetOptionCount < 0 || option < 0 || option >= OptionSlots)
        return;
    WriteAt<int>(_controller, _offsetOptionCount + option * static_cast<int>(sizeof(int)), value);
}

void Vote::ResetBallots()
{
    for (int option = 0; option < OptionSlots; ++option)
        SetOptionCount(option, 0);

    if (_offsetVotesCast < 0)
        return;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        WriteAt<int>(_controller, _offsetVotesCast + slot * static_cast<int>(sizeof(int)), VoteUncast);
}

bool Vote::StartVote(const std::string& title, const std::string& detail, float durationSec, int callerSlot,
                     ResultFn onResult, FinishedFn onFinished)
{
    if (_inProgress || !onResult)
        return false;

    // Subscribed here rather than in a separate arming call: a vote that counted no ballots
    // because nobody armed the service is a silent failure with no good diagnostic.
    if (!_voteCastSub)
    {
        _voteCastSub = _events.Listen("vote_cast", [this](IGameEvent* event) {
            if (!event || !_inProgress)
                return;
            OnVoteCast(event->GetPlayerSlot("userid").Get(), event->GetInt("vote_option"));
        });
    }

    if (!AcquireController())
    {
        Log::Warn("Vote: this map has no vote_controller; no vote can be shown.");
        return false;
    }

    auto& schema = _schema;  // local alias keeps the offset block below narrow

    _eligible = 0;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_entities.GetPlayerController(slot))
            ++_eligible;
    }
    if (_eligible <= 0)
        return false;

    ResetBallots();

    if (int offset = schema.GetOffsetOf<int>(VoteControllerSchema, "m_nPotentialVotes"); offset >= 0)
        WriteAt<int>(_controller, offset, _eligible);
    if (int offset = schema.GetOffsetOf<bool>(VoteControllerSchema, "m_bIsYesNoVote"); offset >= 0)
        WriteAt<bool>(_controller, offset, true);
    if (int offset = schema.GetOffsetOf<int>(VoteControllerSchema, "m_iActiveIssueIndex"); offset >= 0)
        WriteAt<int>(_controller, offset, YesNoIssueIndex);
    // Who may vote is decided by the recipients of the VoteStart message, not by this field.
    if (int offset = schema.GetOffsetOf<int>(VoteControllerSchema, "m_iOnlyTeamToVote"); offset >= 0)
        WriteAt<int>(_controller, offset, AllTeams);

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
    _scheduler.Delay(static_cast<int64_t>(durationSec * 1000.0f), [this, voteId] {
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
        _scheduler.NextTick([this, voteId] {
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

    if (_controller)
    {
        if (int offset = _schema.GetOffsetOf<int>(VoteControllerSchema, "m_iActiveIssueIndex"); offset >= 0)
            WriteAt<int>(_controller, offset, -1);
    }
    _controller = nullptr;
    _offsetOptionCount = -1;
    _offsetVotesCast = -1;

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
