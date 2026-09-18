#include <VoltMod/Api.hpp>
#include <type_traits>

// Glaze publishes no version macro; this one comes from <glaze/core/opts.hpp>, which every glaze
// entry point pulls in.
#ifdef GLZ_NULL_TERMINATED
#error "<VoltMod/Api.hpp> must not reach glaze - route JSON-backed config through <VoltMod/App/Config.hpp>"
#endif

void VoltmodApiSurface_RootLinks(VoltMod::Runtime& runtime)
{
    (void)runtime;
}

class ApiSurfacePlugin final : public VoltMod::Plugin
{
public:
    explicit ApiSurfacePlugin(VoltMod::Runtime& runtime) : Plugin(runtime) {}

private:
    bool Load() override { return true; }
};

static_assert(std::is_base_of_v<VoltMod::Plugin, ApiSurfacePlugin>);
