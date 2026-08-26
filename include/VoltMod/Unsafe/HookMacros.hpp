#pragma once

#include <VoltMod/Core/Subscription.hpp>

// SourceHook's SH_DECL_HOOKn must still appear exactly once at namespace scope in your .cpp -
// it expands to hook-manager classes and cannot be wrapped by a function-scope helper. What CAN
// be automated is the add/remove pairing: VOLTMOD_SCOPED_HOOK installs the hook and yields a
// Subscription that removes it. Store it wherever the hook should live.
//
// `handler` is usually SH_MEMBER(this, &MyPlugin::Hook_Fn).
//
//   SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);
//   void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime)
//   {
//       _listening = VOLTMOD_SCOPED_HOOK(IVEngineServer2, SetClientListening, runtime.Interfaces.Engine,
//                                       SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
//   }
#define VOLTMOD_SCOPED_HOOK(Iface, Func, ifacePtr, handler, post)                                              \
    ([&] {                                                                                                     \
        auto* voltmodHookIface = (ifacePtr);                                                                   \
        SH_ADD_HOOK(Iface, Func, voltmodHookIface, handler, (post));                                           \
        return VoltMod::Subscription([=] { SH_REMOVE_HOOK(Iface, Func, voltmodHookIface, handler, (post)); }); \
    }())
