#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @file UiFields.hpp
 * @brief The `CCSCustomHudLayout` half of @ref UiPanel: schema fields in, engine setters out.
 *
 * Everything here takes the layout as `(entities, ref)` and resolves it per call, so nothing holds
 * an entity pointer across a frame. `entities` may be null and `ref` may be dead - both come back
 * as `Error::NotFound` rather than a crash, which is what lets an empty @ref UiPanel answer every call.
 *
 * A @p slot of @ref kEveryone writes the layout's global state; any other value writes one
 * player's, which the engine networks through a single-slot recipient filter. Internal to `src/`:
 * plugins drive this through @ref UiPanel.
 */

/** Slot value meaning "write the global state", not one player's. */
inline constexpr int kEveryone = -1;

/** How many per-player states the entity carries, or -1 when it or the field is unavailable. */
int UiPlayerStateCount(EntitySystem* entities, EntityRef ref);

/** Set the dialog variable a `text="{s:variable}"` attribute reads. */
Status UiWriteText(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId, std::string_view variable,
                   std::string_view value);

/** Add (@p on) or remove @p className on @p panelId. */
Status UiWriteClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                    std::string_view className, bool on);

/** Hand @p className back to whatever the layout markup itself says. */
Status UiResetClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                    std::string_view className);

/** Give a player (or everyone) a cursor over the layout. */
Status UiWriteInputCapture(EntitySystem* entities, EntityRef ref, int slot, bool enabled);

/** Whether @p slot currently has a cursor. */
Result<bool> UiReadInputCapture(EntitySystem* entities, EntityRef ref, int slot);

}  // namespace VoltMod
