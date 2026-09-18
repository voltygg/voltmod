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
    void Publish(std::string_view name, void* implementation) override
    {
        _entries[std::string(name)] = implementation;
    }

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

/** An exchange attached to its own table, as Plugin::Attach wires one up. */
struct Attached
{
    FakeServices Services;
    ServiceExchange Exchange;

    Attached() { Exchange.Attach(&Services); }
};

TEST_CASE("Get returns the published interface subobject, under its own name only")
{
    Attached host;
    auto& exchange = host.Exchange;
    Both impl;
    exchange.Publish<ICounter>(&impl);

    // ICounter is the second base, at a non-zero offset: 9 rather than 7 proves the right vtable.
    ICounter* found = exchange.Get<ICounter>();
    REQUIRE(found != nullptr);
    CHECK(found->Count() == 9);
    CHECK(exchange.Get<IGreeter>() == nullptr);
}

TEST_CASE("Unpublish withdraws only the named interface")
{
    Attached host;
    auto& exchange = host.Exchange;
    Both impl;
    exchange.Publish<IGreeter>(&impl);
    exchange.Publish<ICounter>(&impl);

    exchange.Unpublish<IGreeter>();

    CHECK(exchange.Find(IGreeter::InterfaceName) == nullptr);
    CHECK(exchange.Find(ICounter::InterfaceName) != nullptr);
}

TEST_CASE("An exchange with no host attached publishes nowhere and finds nothing")
{
    ServiceExchange exchange;
    Both impl;
    exchange.Publish<IGreeter>(&impl);

    CHECK(exchange.Get<IGreeter>() == nullptr);
}
