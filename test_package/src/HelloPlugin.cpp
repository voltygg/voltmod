#include <VoltMod/Api.hpp>

class HelloPlugin final : public VoltMod::MetamodPlugin
{
protected:
    VoltMod::PluginInfo Info() const override
    {
        return {.Name = "Hello", .Author = "voltmod test_package", .LogTag = "HELLO"};
    }

    bool OnLoad(VoltMod::Runtime& /*runtime*/) override { return true; }
};

VOLTMOD_PLUGIN(HelloPlugin);
