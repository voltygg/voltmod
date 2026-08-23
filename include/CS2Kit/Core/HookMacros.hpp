#pragma once

// SourceHook's SH_DECL_HOOKn must still appear exactly once at namespace scope in your .cpp -
// it expands to hook-manager classes and cannot be wrapped by a function-scope helper. What CAN
// be automated is the add/remove pairing: CS2KIT_SCOPED_HOOK installs the hook and queues the
// matching removal on the plugin's Defer stack in one statement.
//
// Use inside a MetamodPluginBase member (typically OnRegisterHooks); `handler` is usually
// SH_MEMBER(this, &MyPlugin::Hook_Fn).
//
//   SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);
//   void MyPlugin::OnRegisterHooks()
//   {
//       CS2KIT_SCOPED_HOOK(IVEngineServer2, SetClientListening, Engine().Sdk.Interfaces.Engine,
//                          SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
//   }
#define CS2KIT_SCOPED_HOOK(Iface, Func, ifacePtr, handler, post)                                                      \
    do                                                                                                                \
    {                                                                                                                 \
        SH_ADD_HOOK(Iface, Func, (ifacePtr), handler, (post));                                                        \
        this->Defer(                                                                                                  \
            [this, cs2kitHookIface = (ifacePtr)] { SH_REMOVE_HOOK(Iface, Func, cs2kitHookIface, handler, (post)); }); \
    }                                                                                                                 \
    while (0)
