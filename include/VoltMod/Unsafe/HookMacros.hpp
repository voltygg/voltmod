#pragma once

#include <VoltMod/Core/Subscription.hpp>

// Declare SH_DECL_HOOKn once at namespace scope in the .cpp. This macro pairs SH_ADD_HOOK with
// SH_REMOVE_HOOK and returns a Subscription; store it for the desired hook lifetime. `handler`
// is usually SH_MEMBER(this, &MyPlugin::Hook_Fn).
//
//   SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);
//   void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime)
//   {
//       _listening = VOLTMOD_SCOPED_HOOK(IVEngineServer2, SetClientListening, runtime.Unsafe.Interfaces.Engine,
//                                       SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
//   }
#define VOLTMOD_SCOPED_HOOK(Iface, Func, ifacePtr, handler, post)                                              \
    ([&] {                                                                                                     \
        auto* voltmodHookIface = (ifacePtr);                                                                   \
        SH_ADD_HOOK(Iface, Func, voltmodHookIface, handler, (post));                                           \
        return VoltMod::Subscription([=] { SH_REMOVE_HOOK(Iface, Func, voltmodHookIface, handler, (post)); }); \
    }())
