#include <VoltMod/App/ServiceExchange.hpp>
#include <doctest/doctest.h>

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

// No Get() cases: it goes through MetaFactory, which only exists in a loaded plugin. What is
// testable here is the table OnMetamodQuery serves.

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

    // The second base is at a non-zero offset; recovering 9 rather than 7 proves the cast
    // lands on the right vtable.
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
