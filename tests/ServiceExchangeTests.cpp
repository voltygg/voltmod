#include <CS2Kit/Core/ServiceExchange.hpp>
#include <doctest/doctest.h>

using CS2Kit::Core::ServiceExchange;

namespace
{
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

// Implements both, so Publish<T> has to store the T subobject rather than the object address.
struct Both final : IGreeter, ICounter
{
    int Greet() override { return 7; }
    int Count() override { return 9; }
};
}  // namespace

// Get() is deliberately absent from these cases: it routes through Metamod's MetaFactory, which
// only exists in a loaded plugin. What is testable here is the table OnMetamodQuery serves.

TEST_CASE("An unpublished interface is not found")
{
    ServiceExchange exchange;
    CHECK(exchange.Find(IGreeter::InterfaceName) == nullptr);
}

TEST_CASE("A published interface is found under its own name only")
{
    ServiceExchange exchange;
    Both impl;
    exchange.Publish<IGreeter>(&impl);

    CHECK(exchange.Find(IGreeter::InterfaceName) == static_cast<IGreeter*>(&impl));
    CHECK(exchange.Find(ICounter::InterfaceName) == nullptr);
}

TEST_CASE("Publish stores the interface subobject not the object address")
{
    ServiceExchange exchange;
    Both impl;
    exchange.Publish<ICounter>(&impl);

    // The second base sits at a non-zero offset, so a consumer casting the void* back to ICounter
    // must land on the right vtable. Recovering 9 rather than 7 is what proves it.
    auto* recovered = static_cast<ICounter*>(exchange.Find(ICounter::InterfaceName));
    REQUIRE(recovered != nullptr);
    CHECK(recovered->Count() == 9);
}

TEST_CASE("Unpublish withdraws only the named interface")
{
    ServiceExchange exchange;
    Both impl;
    exchange.Publish<IGreeter>(&impl);
    exchange.Publish<ICounter>(&impl);

    exchange.Unpublish<IGreeter>();

    CHECK(exchange.Find(IGreeter::InterfaceName) == nullptr);
    CHECK(exchange.Find(ICounter::InterfaceName) != nullptr);
}

TEST_CASE("Publishing twice replaces the earlier implementation")
{
    ServiceExchange exchange;
    Both first;
    Both second;
    exchange.Publish<IGreeter>(&first);
    exchange.Publish<IGreeter>(&second);

    CHECK(exchange.Find(IGreeter::InterfaceName) == static_cast<IGreeter*>(&second));
}
