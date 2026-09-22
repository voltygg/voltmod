#include "Host/Plugins/CallbackList.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::CallbackList;

using Notify = void (*)(void* context);

/** What ran, plus the one edit a callback makes to the list it is running in. */
struct ListTrace
{
    CallbackList<Notify>* List = nullptr;
    uint64_t Target = 0;
    int Passes = 0;
    std::vector<std::string> Calls;
};

static ListTrace& Trace(void* context)
{
    return *static_cast<ListTrace*>(context);
}

static void Run(CallbackList<Notify>& list)
{
    list.Dispatch([](Notify callback, void* context) {
        callback(context);
        return false;
    });
}

TEST_CASE("Callbacks run in the order their owners loaded in")
{
    CallbackList<Notify> list;
    ListTrace trace;
    list.Add(1, 2, +[](void* context) { Trace(context).Calls.push_back("later"); }, &trace);
    list.Add(2, 1, +[](void* context) { Trace(context).Calls.push_back("earlier"); }, &trace);

    Run(list);

    CHECK(trace.Calls == std::vector<std::string>{"earlier", "later"});
}

TEST_CASE("A callback removed earlier in the same pass does not run")
{
    CallbackList<Notify> list;
    ListTrace trace;
    trace.List = &list;

    list.Add(
        1, 1,
        +[](void* context) {
            ListTrace& state = Trace(context);
            state.Calls.push_back("first");
            state.List->Remove(state.Target);
        },
        &trace);
    trace.Target = 2;
    list.Add(2, 1, +[](void* context) { Trace(context).Calls.push_back("second"); }, &trace);
    list.Add(3, 1, +[](void* context) { Trace(context).Calls.push_back("third"); }, &trace);

    Run(list);
    CHECK(trace.Calls == std::vector<std::string>{"first", "third"});

    trace.Calls.clear();
    Run(list);
    CHECK(trace.Calls == std::vector<std::string>{"first", "third"});
}

TEST_CASE("A callback added during a pass first runs in the next one")
{
    CallbackList<Notify> list;
    ListTrace trace;
    trace.List = &list;

    list.Add(
        1, 1,
        +[](void* context) {
            ListTrace& state = Trace(context);
            state.Calls.push_back("first");
            if (state.Passes++ > 0)
            {
                return;
            }
            state.List->Add(2, 1, +[](void* inner) { Trace(inner).Calls.push_back("late"); }, context);
        },
        &trace);

    Run(list);
    CHECK(trace.Calls == std::vector<std::string>{"first"});

    trace.Calls.clear();
    Run(list);
    CHECK(trace.Calls == std::vector<std::string>{"first", "late"});
}

TEST_CASE("A dispatch stops at the first callback that answers")
{
    CallbackList<bool (*)(void* context)> list;
    ListTrace trace;
    list.Add(
        1, 1,
        +[](void* context) {
            Trace(context).Calls.push_back("first");
            return true;
        },
        &trace);
    list.Add(
        2, 1,
        +[](void* context) {
            Trace(context).Calls.push_back("second");
            return false;
        },
        &trace);

    const bool stopped = list.Dispatch([](bool (*callback)(void*), void* context) { return callback(context); });

    CHECK(stopped);
    CHECK(trace.Calls == std::vector<std::string>{"first"});
}
