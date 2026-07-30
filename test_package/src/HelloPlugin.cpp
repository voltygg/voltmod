#include <CS2Kit/Api.hpp>

namespace Hello
{
struct Managers
{
};
Managers& App();
}  // namespace Hello

class HelloPlugin : public CS2Kit::PluginBase<Hello::Managers>
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return {.Name = "Hello", .Author = "cs2-kit test_package", .LogTag = "HELLO"};
    }

    bool OnLoad(bool /*late*/) override { return true; }
};

CS2KIT_PLUGIN(HelloPlugin, Hello);
