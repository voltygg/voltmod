#include <VoltMod/App/ServiceExchange.hpp>
#include <doctest/doctest.h>
#include <string>
#include <unordered_map>

using VoltMod::ServiceExchange;

struct IGreeter
{
    static constexpr const char* InterfaceName = "test.IGreeter/1";
    virtual int Greet() = 0;

protected:
    ~IGreeter() = default;
};

struct ICounter
{
    static constexpr const char* InterfaceName = "test.ICounter/1";
    virtual int Count() = 0;

protected:
    ~ICounter() = default;
};

// Implements both, so Publish<T> must store the T subobject, not the object address.
struct Both final : IGreeter, ICounter
{
    int Greet() override { return 7; }
    int Count() override { return 9; }
};

// The host owns the real table; this stands in for it so the exchange has somewhere to publish.
class FakeServices final : public VoltMod::IHostServices
{
public:
    void Publish(std::string_view name, void* implementation) override { _entries[std::string(name)] = implementation; }

    void Unpublish(std::string_view name) override { _entries.erase(std::string(name)); }

    void* Find(std::string_view name) override
    {
        auto it = _entries.find(std::string(name));
        return it == _entries.end() ? nullptr : it->second;
    }

    uint64_t OnChanged(ChangedFn, void*) override { return 0; }
    void Unsubscribe(uint64_t) override {}

private:
    std::unordered_map<std::string, void*> _entries;
};

/** An exchange over its own table, as the plugin module wires one up. */
struct Attached
{
    FakeServices Services;
    ServiceExchange Exchange{Services};
};

TEST_CASE("Get returns the published interface subobject, under its own name only")
{
    Attached host;
    auto& exchange = host.Exchange;
    Both impl;
    auto published = exchange.Publish<ICounter>(&impl);

    // ICounter is the second base, at a non-zero offset: 9 rather than 7 proves the right vtable.
    ICounter* found = exchange.Get<ICounter>();
    REQUIRE(found != nullptr);
    CHECK(found->Count() == 9);
    CHECK(exchange.Get<IGreeter>() == nullptr);
}

TEST_CASE("Dropping the subscription withdraws only that interface")
{
    Attached host;
    auto& exchange = host.Exchange;
    Both impl;
    auto greeter = exchange.Publish<IGreeter>(&impl);
    auto counter = exchange.Publish<ICounter>(&impl);

    greeter.Reset();

    CHECK(exchange.Get<IGreeter>() == nullptr);
    CHECK(exchange.Get<ICounter>() != nullptr);
}

TEST_CASE("Keyed providers of one interface are found and withdrawn apart")
{
    Attached host;
    auto& exchange = host.Exchange;
    Both first;
    Both second;
    auto firstEntry = exchange.Publish<ICounter>(&first, "first");
    auto secondEntry = exchange.Publish<ICounter>(&second, "second");

    CHECK(exchange.Get<ICounter>("first") == &first);
    CHECK(exchange.Get<ICounter>() == nullptr);

    firstEntry.Reset();

    CHECK(exchange.Get<ICounter>("first") == nullptr);
    CHECK(exchange.Get<ICounter>("second") == &second);
}
