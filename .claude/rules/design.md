---
paths:
  - "include/**"
  - "src/**"
---

# API design

The shapes the framework already uses. New code follows them instead of adding a second way.

## Runtime and injection

- `MetamodPlugin` owns the Metamod entry points and one `Runtime` per load cycle, passed to `OnLoad(Runtime&)`. Consumers release their state in `OnUnload`.
- `Runtime` is a flat service container (`runtime.Players`, `runtime.Messages`), so moving a service between modules does not rename the consumer API.
- No ambient accessor. Constructor-inject the narrowest service that does the job: `CenterHtmlMenu(const CenterHtmlMenu::Services&)`, `ActionDispatcher(Policy&, PlayerManager&, EntitySystem&)`, never `Runtime&`. Only `Commands` and `App` may take `Runtime&`.
- Header templates plugins instantiate (`Flow<TState>`, `PerSlot<T>`) take one service, so including them does not pull in the composition root.
- No process-lifetime singletons.

## Registration and authorization

- Descriptors such as `Action` and menu rows are registered explicitly at load, never at static init.
- Commands use `Commands.Add(name)...Run(handler)`; the handler signature is its argument spec.
- Consumers inject permission, targeting, reply, and broadcast behavior once through `Runtime::Policy`. `Policy::Authorize(caller, target, permission)` is the only gate; nothing repeats its steps. Denial is a `Result<Authorized>` error, not a nulled field.

## Players and events

- `PlayerRef` is what gets stored, `Player&` is who is connected now, `Controller`/`Pawn` are this frame's entities.
- `PlayerManager` owns the roster and raises `Connected`, `FullyConnected`, `SettingsChanged`, `Disconnected`. No lifecycle virtuals on `MetamodPlugin`.
- A signal is a public `Event<Args...>` member. `+=` is the only way to subscribe; `Raise` belongs to the owner.
- Game events go through `GameEvents::On<T>` with a struct in `Events/EventTypes.hpp`. No string form.
- An `Event` whose source costs something takes an `EventLifecycle`: first subscription installs, last drop removes, and `OnFirst` returning false refuses after logging why. One source feeding several events uses a `SharedLifecycle` instead of counting subscribers itself.

## Capabilities

- Services do not expose `Install()`, `Enable()`, `Available()`, or similar flags.
- `Runtime::Start` records what works this load in `Runtime::Capabilities`, with the reason when it is off. A service whose capability is off is inert and safe to call.

## Hooks and subscriptions

- A vtable hook is a `HookVTable` or `HookInstance` call from `<VoltMod/Unsafe/Hook.hpp>` plus the `Subscription` it returns, which removes the hook when dropped. Handlers take the hooked object first and return `KHook::Return`. Only a .cpp reaches `Hook.hpp`; a header that holds a hook needs `<VoltMod/Core/Subscription.hpp>` alone.
- Event, game-event, scheduler, and hook registrations return `[[nodiscard]] Subscription`. Dropping it unsubscribes; a scheduler one-shot is cancelled the same way.
- Commands are owned by `CommandManager` for the load cycle.

## Errors

- Fallible operations return `Result<T>`/`Status` over `Error`: `ErrorCode` to branch on, `Detail` for the log, `Key` for the player-facing reply.

## Entities and schema

- `Entity`, `Pawn`, `Controller` are frame-local wrappers. `explicit operator bool()` is the only validity check; they copy but do not assign. Anything stored is an `EntityRef`/`PlayerRef` re-resolved through `EntitySystem`.
- A schema field is a generated `Health()`/`SetHealth()` pair. `voltmod schemagen` bakes the offset from `schema/manifest.json` plus a dump; the setter dirties the write through the entity, a `__m_pChainEntity` chainer, or the enclosing entity.
- No schema service, no runtime resolution, no string lookup on a call path. `Runtime::Start` aborts the load if the baked layout and live schema disagree.

## Gamedata, convars, enums

- `gamedata/gamedata.jsonc` says only *where* something is. `Engine/Bindings.hpp` owns every prototype, vtable signature, and field type; services take `const Bindings&` and read typed fields.
- Parsing lives in `src/Engine/GameDataFile.*` and stays SDK-free so it is unit-tested.
- One convar is one `ConVar<T>`, resolved by name once. `Set` uses a cfg line so replicated values reach clients; `RawScope` pokes storage without callbacks or networking.
- Enumerator names come from `Core/EnumNames.hpp` (`Name(value)`, `Parse<E>(text)`), not hand-written switches.

## Threading

- Database and HTTP workers replay completions on the game thread through the scheduler's per-frame delivery.

## Public headers

- `<VoltMod/Api.hpp>` gathers the core vocabulary, `Runtime`, players, commands, and plugin plumbing. It never reaches the JSON layer or the menu-building surface.
- Module surfaces: `<VoltMod/Entities/Api.hpp>`, `<VoltMod/Hooks/Api.hpp>`, `<VoltMod/Menu/Api.hpp>`, `<VoltMod/Unsafe/Api.hpp>`. `<VoltMod/App/Config.hpp>` gathers `JsonConfig`, `StandardPluginSettings`, and `Json` for a plugin's `Config.hpp`.
- Database names stay in `<VoltMod/Database/Api.hpp>` so ordinary TUs never include libpqxx.
