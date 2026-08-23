#include <CS2Kit/Api.hpp>

class HelloPlugin final : public CS2Kit::MetamodPlugin
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return {.Name = "Hello", .Author = "cs2-kit test_package", .LogTag = "HELLO"};
    }

    bool OnLoad(CS2Kit::Runtime& /*runtime*/, bool /*late*/) override { return true; }
};

CS2KIT_PLUGIN(HelloPlugin);
