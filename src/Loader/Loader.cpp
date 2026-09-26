#include "Loader/Loader.hpp"

#include "Host/HostStart.hpp"
#include "Loader/GameInfo.hpp"
#include "Loader/HookDispatcher.hpp"

#include <cstring>
#include <eiface.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <khook.hpp>
#include <khook/memory.hpp>
#include <string>
#include <system_error>
#include <tier0/dbg.h>
#include <tier0/icommandline.h>
#include <utility>

namespace VoltMod
{

static HookDispatcher g_dispatcher;
static std::string g_gameDir;
static HostStart g_start{.HookDispatcher = &g_dispatcher};
static HostStopFn g_stopHost = nullptr;

// What the swapped slots held. Never written back: Metamod restores its own slots, and ours would undo that.
static bool (*g_connect)(void* config, InterfaceFactory engineFactory) = nullptr;
static void (*g_disconnect)(void* config) = nullptr;
static InitReturnVal_t (*g_init)(void* server) = nullptr;
static void (*g_shutdown)(void* server) = nullptr;

/** Put @p replacement in slot @p index of @p object's vtable, and return what the slot held. */
template <class Fn>
static Fn SwapSlot(void* object, int index, Fn replacement)
{
    void** slot = *static_cast<void***>(object) + index;
    KHook::Memory::SetAccess(slot, sizeof(void*),
                             KHook::Memory::Flags::READ | KHook::Memory::Flags::WRITE | KHook::Memory::Flags::EXECUTE);
    const Fn original = reinterpret_cast<Fn>(*slot);
    *slot = reinterpret_cast<void*>(replacement);
    return original;
}

/** `csgo/addons/voltmod/bin/<platform>/` holds this module, so `csgo` is four directories up. */
static std::filesystem::path GameDir()
{
    std::filesystem::path dir = OwnPath().parent_path();
    for (int level = 0; level < 4; ++level)
    {
        dir = dir.parent_path();
    }
    return dir;
}

static std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/** The first server library on the `Game` paths: Metamod's loader where it is installed, else the game's. */
static InterfaceFactory LoadNextServer()
{
    // Answering nothing makes the engine load `server` itself, so a game client never runs VoltMod.
    if (!CommandLine()->HasParm("-dedicated"))
    {
        return nullptr;
    }

    const std::filesystem::path gameDir = GameDir();
    const std::filesystem::path gameinfo = gameDir / "gameinfo.gi";
    const std::string text = ReadFile(gameinfo);
    if (text.empty())
    {
        Warning("[VoltMod] Could not read %s, so VoltMod is not loaded.\n", gameinfo.string().c_str());
        return nullptr;
    }

    for (const std::string& searchPath : GameSearchPaths(text))
    {
        const std::filesystem::path server = gameDir.parent_path() / searchPath / "bin" / PlatformDir / ServerFile;
        std::error_code missing;
        if (!std::filesystem::is_regular_file(server, missing))
        {
            continue;
        }

        void* module = OpenModule(server);
        auto* factory = module ? reinterpret_cast<InterfaceFactory>(FindExport(module, "CreateInterface")) : nullptr;
        if (factory == nullptr)
        {
            Warning("[VoltMod] Could not load %s: %s\n", server.string().c_str(), LastError().c_str());
            continue;
        }

        Msg("[VoltMod] Loader started; forwarding the game server to %s\n", server.string().c_str());
        g_gameDir = gameDir.string();
        g_start.GameDir = g_gameDir.c_str();
        return factory;
    }

    Warning("[VoltMod] No Game path in %s holds a server library, so VoltMod is not loaded.\n",
            gameinfo.string().c_str());
    return nullptr;
}

static InterfaceFactory FindNextFactory()
{
    InterfaceFactory next = LoadNextServer();
    if (next == nullptr)
    {
        // The engine may unload this module now; KHook's threads must be joined first.
        KHook::Shutdown();
    }
    return next;
}

static void StartHost()
{
    if (g_start.EngineFactory == nullptr || g_start.ServerFactory == nullptr)
    {
        Warning("[VoltMod] The engine and game factories were not found, so VoltMod is not loaded.\n");
        return;
    }

    const std::filesystem::path path = OwnPath().parent_path() / HostFile;
    void* host = OpenModule(path);
    if (host == nullptr)
    {
        Warning("[VoltMod] Could not load %s: %s\n", path.string().c_str(), LastError().c_str());
        return;
    }

    auto* start = reinterpret_cast<HostStartFn>(FindExport(host, HostStartName));
    auto* stop = reinterpret_cast<HostStopFn>(FindExport(host, HostStopName));
    if (start == nullptr || stop == nullptr)
    {
        Warning("[VoltMod] Could not load %s: %s\n", path.string().c_str(), LastError().c_str());
        return;
    }

    // The server still runs without VoltMod, as it would after a failed Metamod plugin.
    char error[512] = "";
    if (!start(&g_start, error, sizeof(error)))
    {
        Warning("[VoltMod] The host did not start: %s\n", error);
        return;
    }
    g_stopHost = stop;
}

static bool OnConnect(void* config, InterfaceFactory engineFactory)
{
    g_start.EngineFactory = engineFactory;
    return g_connect(config, engineFactory);
}

static InitReturnVal_t OnInit(void* server)
{
    // First, so Metamod's plugins start after VoltMod and hook on top of it.
    StartHost();
    return g_init(server);
}

static void StopHost()
{
    if (g_stopHost != nullptr)
    {
        std::exchange(g_stopHost, nullptr)();
    }
}

static void OnShutdown(void* server)
{
    // Here, not at Disconnect: by then the engine has shut down and a plugin's console command crashes it.
    StopHost();
    g_shutdown(server);
}

static void OnDisconnect(void* config)
{
    StopHost();
    // Safe: this slot was swapped, not hooked through KHook.
    KHook::Shutdown();
    g_disconnect(config);
}

static void Patch(const char* name, void* object)
{
    if (std::strcmp(name, INTERFACEVERSION_SERVERCONFIG) == 0 && g_connect == nullptr)
    {
        g_connect = SwapSlot(object, KHook::GetVtableIndex(&ISource2ServerConfig::Connect), &OnConnect);
        g_disconnect = SwapSlot(object, KHook::GetVtableIndex(&ISource2ServerConfig::Disconnect), &OnDisconnect);
    }
    else if (std::strcmp(name, INTERFACEVERSION_SERVERGAMEDLL) == 0 && g_init == nullptr)
    {
        // The game's own factory: asking Metamod's loader for this would patch Init again.
        g_start.ServerFactory = FactoryAt(*static_cast<void**>(object));
        g_init = SwapSlot(object, KHook::GetVtableIndex(&ISource2Server::Init), &OnInit);
        g_shutdown = SwapSlot(object, KHook::GetVtableIndex(&ISource2Server::Shutdown), &OnShutdown);
    }
}

}  // namespace VoltMod

void* CreateInterface(const char* name, int* returnCode)
{
    static const VoltMod::InterfaceFactory next = VoltMod::FindNextFactory();
    if (next == nullptr)
    {
        if (returnCode != nullptr)
        {
            *returnCode = IFACE_FAILED;
        }
        return nullptr;
    }

    void* object = next(name, returnCode);
    if (object != nullptr)
    {
        VoltMod::Patch(name, object);
    }
    return object;
}
