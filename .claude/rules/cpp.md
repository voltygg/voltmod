---
paths:
  - "include/**"
  - "src/**"
  - "tests/**"
  - "templates/**"
---

# C++ conventions

## Style

- C++23, `.hpp` headers, `#pragma once`.
- Prefer `std::format`, designated initializers, and `std::function` callbacks.
- `PascalCase` types and methods, `_camelCase` members.

## Namespaces

- Every public name lives in `VoltMod`. Modules are directories and layers, not namespaces.
- The only nested namespaces: free-function groups with a common noun (`Log`, `ChatColors`, `Validation`, `PawnOps`); `Args`, the command argument types that only appear in handler signatures; and `Internal`, which may only appear under `src/`.
- No anonymous namespaces; a file-local helper is `static` at the top of the .cpp.
- No using-directives. A .cpp may write `using VoltMod::Player;`; a header may not.

## Includes and forward declarations

- Include the header that defines a type.
- Forward declarations live only in `include/VoltMod/Engine/EngineTypes.hpp`, which `modgraph` knows by path. Each entry says why: an SDK type, a type defined under `src/`, or a mutually owning pair.
- A header declaring a name it goes on to define (a primary template before its specializations) is ordering its own contents, not forward-declaring.

## File-statics

Allowed only for:

- An engine callback with no user data, set and cleared by the owning service.
- Process-wide state set once at load: `Log::Handler`, the base directory.
- State that is genuinely process-wide rather than per-load. `g_schema` in `src/Schema/Verify.cpp` is the one example: a single engine object whose offsets are constants of the loaded binary.

## Tests and templates

- Tests use doctest and stay HL2SDK-free. Case names must not contain `[`, `]`, or `;`.
- `templates/` and doc examples must keep compiling against the public headers.
