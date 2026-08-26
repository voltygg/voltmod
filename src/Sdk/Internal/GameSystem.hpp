#pragma once

// igamesystem.h assumes these are already in the TU: CSplitScreenSlot lives in
// convar.h; entitysystem.h supplies CUtlString/CEntityHandle/SpawnGroupHandle_t.
#include <entity2/entitysystem.h>
#include <igamesystem.h>
#include <igamesystemfactory.h>
#include <tier1/convar.h>
#include <tier1/utlvector.h>

/**
 * The SDK forward-declares IEntityResourceManifest (global scope) but never
 * defines it; this definition completes that type and is copied from CS2Fixes
 * (src/gamesystem.h). The overload order is based on the Linux binary - MSVC
 * emits overloaded virtuals in reverse declaration order, which flips it to
 * match the Windows binary.
 */
class IEntityResourceManifest
{
public:
    virtual void AddResource(const char*) = 0;
    virtual void AddResource(const char*, void*) = 0;
    virtual void AddResource(const char*, void*, void*, void*) = 0;
    virtual void unk_04() = 0;
    virtual void unk_05() = 0;
    virtual void unk_06() = 0;
    virtual void unk_07() = 0;
    virtual void unk_08() = 0;
    virtual void unk_09() = 0;
    virtual void unk_10() = 0;
};

namespace VoltMod::Sdk
{

class PrecacheService;

/**
 * Engine-side layout mirrors (copied from CS2Fixes src/gamesystem.h). Only
 * touched during PrecacheService::Shutdown to detach the live game system.
 */
class IGameSystemEventDispatcher
{
public:
    virtual ~IGameSystemEventDispatcher() {}
};

class CGameSystemEventDispatcher : public IGameSystemEventDispatcher
{
public:
    CUtlVector<CUtlVector<IGameSystem*>>* m_funcListeners;
};

struct AddedGameSystem_t
{
    IGameSystem* m_pGameSystem;
    int m_nPriority;
    int m_nInsertionOrder;
};

/**
 * Self-contained stand-in for the SDK's CBaseGameSystemFactory. The engine walks
 * factory objects through the IGameSystemFactory vtable plus the next/name fields,
 * so those must stay the first data members in this order. The SDK class is not
 * usable directly: its list head is an unresolved class-static and its link fields
 * are private, so it cannot be unlinked on plugin unload.
 */
class GameSystemFactory final : public IGameSystemFactory
{
public:
    GameSystemFactory(const char* name, IGameSystem* system, GameSystemFactory** listHead)
        : _next(*listHead), _name(name), _system(system), _listHead(listHead)
    {
        // The engine matches a reallocating system's GetName() against the factory names to find
        // its factory, so the two must agree; CGameSystemStaticFactory names the system here for
        // the same reason. Without it the lookup fails on level teardown and the engine faults on
        // the miss. @p name must outlive this factory.
        _system->SetName(name);
        *listHead = this;
    }

    /** Unlink this factory from the engine's list. Idempotent. */
    void Unregister()
    {
        for (GameSystemFactory** cursor = _listHead; *cursor; cursor = &(*cursor)->_next)
        {
            if (*cursor == this)
            {
                *cursor = _next;
                return;
            }
        }
    }

    bool Init() override { return _system->Init(); }
    void PostInit() override { _system->PostInit(); }
    void Shutdown() override { _system->Shutdown(); }

    IGameSystem* CreateGameSystem() override
    {
        _system->SetGameSystemGlobalPtrs(_system);
        return _system;
    }

    void DestroyGameSystem(IGameSystem*) override { _system->SetGameSystemGlobalPtrs(nullptr); }
    bool ShouldAutoAdd() override { return true; }
    int GetPriority() override { return 0; }
    void SetGlobalPtr(void*) override {}
    bool IsReallocating() override { return false; }
    IGameSystem* GetStaticGameSystem() override { return _system; }

private:
    // Engine-visible fields; the list also holds engine-owned factories, and the
    // traversal above reads their next pointers through this same layout.
    GameSystemFactory* _next;
    const char* _name;
    // Framework-only fields (the engine never reads past the name).
    IGameSystem* _system;
    GameSystemFactory** _listHead;
};

/**
 * Framework-owned game system: forwards BuildGameSessionManifest to the owning
 * PrecacheService so queued resource paths reach the session manifest.
 */
class PrecacheGameSystem final : public CBaseGameSystem
{
public:
    explicit PrecacheGameSystem(PrecacheService& owner) : _owner(owner) {}

    DECLARE_GAME_SYSTEM();
    GS_EVENT(BuildGameSessionManifest);

    // Mirrors CS2Fixes' battle-tested combination for statically-owned systems.
    bool DoesGameSystemReallocate() override { return true; }

private:
    PrecacheService& _owner;
};

}  // namespace VoltMod::Sdk
