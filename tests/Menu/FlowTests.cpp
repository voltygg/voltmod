#include "FakeMenuSession.hpp"

#include <VoltMod/Menu/Flow.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using VoltModTests::FakeMenuSession;

/** The state a test flow threads through its steps. */
struct FlowTestState
{
    std::vector<std::string> Visited;
    bool Timed = true;
    int Finished = 0;
};

using TestFlow = VoltMod::Flow<FlowTestState>;

// A step whose one row records the step's name and moves on. The flow is captured as a pointer,
// not as the reference the factory is handed: that reference is the factory call's, while the row
// outlives it.
static TestFlow::BuildFn StepNamed(std::string name)
{
    return [name](TestFlow& flow) {
        TestFlow* self = &flow;
        return VoltMod::MenuBuilder(name)
            .Button("go",
                    [self, name](int) {
                        self->State().Visited.push_back(name);
                        self->Advance();
                    })
            .Build();
    };
}

TEST_CASE("Flow: steps open in order, one at a time")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->AddStep(StepNamed("first"))
        ->AddStep(StepNamed("second"))
        ->Finish([](FlowTestState& state) { ++state.Finished; })
        ->Start();

    REQUIRE(session.Opened.size() == 1);
    CHECK(session.Last()->Title == "first");

    session.Press(0);
    REQUIRE(session.Opened.size() == 2);
    CHECK(session.Last()->Title == "second");

    session.Press(0);
    CHECK(flow->State().Visited == std::vector<std::string>{"first", "second"});
    CHECK(flow->State().Finished == 1);
}

TEST_CASE("Flow: a step whose Applies is false is skipped")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{.Timed = false});
    flow->AddStep(StepNamed("duration"), [](const FlowTestState& state) { return state.Timed; })
        ->AddStep(StepNamed("reason"))
        ->Finish([](FlowTestState&) {})
        ->Start();

    REQUIRE(session.Opened.size() == 1);
    CHECK(session.Last()->Title == "reason");
}

TEST_CASE("Flow: with no steps and no confirm, Start finishes straight away")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->Finish([](FlowTestState& state) { ++state.Finished; })->Start();

    CHECK(session.Opened.empty());
    CHECK(flow->State().Finished == 1);
    // Finishing closes whatever the flow had on screen.
    CHECK(session.CloseAlls == 1);
}

TEST_CASE("Flow: advancing past the last step finishes")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->AddStep(StepNamed("only"))->Finish([](FlowTestState& state) { ++state.Finished; })->Start();

    REQUIRE(session.Opened.size() == 1);
    session.Press(0);
    CHECK(flow->State().Finished == 1);
    CHECK(session.CloseAlls == 1);
}

TEST_CASE("Flow: a Validate key aborts before the first step, replied and closed")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->Validate([](const FlowTestState&) { return std::optional<std::string>("cmd.targetLost"); })
        ->AddStep(StepNamed("first"))
        ->Finish([](FlowTestState& state) { ++state.Finished; })
        ->Start();

    CHECK(session.Opened.empty());
    CHECK(session.CloseAlls == 1);
    CHECK(session.ReplyKey == "cmd.targetLost");
    CHECK(flow->State().Finished == 0);
}

TEST_CASE("Flow: a Validate key that only starts holding later aborts before finish")
{
    FakeMenuSession session;

    bool valid = true;
    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->Validate([&valid](const FlowTestState&) {
            return valid ? std::nullopt : std::optional<std::string>("cmd.targetLost");
        })
        ->AddStep(StepNamed("first"))
        ->Finish([](FlowTestState& state) { ++state.Finished; })
        ->Start();

    REQUIRE(session.Opened.size() == 1);

    // Anything may change while a step is on screen; the check before finish is what catches it.
    valid = false;
    session.Press(0);
    CHECK(flow->State().Finished == 0);
    CHECK(session.ReplyKey == "cmd.targetLost");
    CHECK(session.CloseAlls == 1);
}

TEST_CASE("Flow: the confirm dialog summarizes the state and its confirm row finishes")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->AddStep(StepNamed("first"))
        ->Confirm({.Title = "Confirm",
                   .Summary =
                       [](const FlowTestState& state) {
                           return std::vector<std::pair<std::string, std::string>>{
                               {"Steps", std::to_string(state.Visited.size())}};
                       },
                   .ConfirmLabel = "Yes",
                   .CancelLabel = "No"})
        ->Finish([](FlowTestState& state) { ++state.Finished; })
        ->Start();

    session.Press(0);
    REQUIRE(session.Opened.size() == 2);
    const VoltMod::Menu* confirm = session.Last();
    CHECK(confirm->Title == "Confirm");
    REQUIRE(confirm->Items.size() == 3);
    CHECK(confirm->Items[0].Describe(0).Label == "Steps: 1");
    CHECK(confirm->Items[1].Describe(0).Label == "Yes");
    CHECK(confirm->Items[2].Describe(0).Label == "No");

    session.Press(1);
    CHECK(flow->State().Finished == 1);
    CHECK(session.CloseAlls == 1);
}

TEST_CASE("Flow: the confirm dialog's cancel row closes without finishing")
{
    FakeMenuSession session;

    auto flow = TestFlow::Create(session, 0, FlowTestState{});
    flow->Confirm({.Title = "Confirm",
                   .Summary = [](const FlowTestState&) { return std::vector<std::pair<std::string, std::string>>{}; },
                   .ConfirmLabel = "Yes",
                   .CancelLabel = "No"})
        ->Finish([](FlowTestState& state) { ++state.Finished; })
        ->Start();

    REQUIRE(session.Opened.size() == 1);
    session.Press(1);
    CHECK(flow->State().Finished == 0);
    CHECK(session.CloseAlls == 1);
}
