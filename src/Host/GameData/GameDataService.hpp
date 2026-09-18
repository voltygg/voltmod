#pragma once

#include "Host/GameData/GameDataDocument.hpp"
#include "Host/GameData/GameDataResolver.hpp"

#include <VoltMod/Engine/Memory/OriginalSlotLookup.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The process's one gamedata resolution, served to every plugin.
 *
 * Read and scanned once at host load, before any plugin runs, and kept for the rest of the
 * process. What did not bind is logged here, once, with the same reason each plugin's
 * @ref Bindings then reports through its own `Available()`.
 */
class GameDataService final : public IHostGameData
{
public:
    /** Read @p path, scan for every entry, log the outcome and record what bound.
     *
     *  @p originalOf reads a vtable slot another module already hooked. */
    void Resolve(std::string_view path, const OriginalSlotLookup& originalOf = {});

    /** False when the file could not be read, in which case there is nothing to serve. */
    bool Ready() const { return _resolver != nullptr; }

    GameDataLocation Lookup(GameDataSection sections, std::string_view key) override;

private:
    /** An entry that did not bind, keeping @p reason until the next lookup. */
    GameDataLocation NotBound(std::string reason);

    // The resolver keeps a reference to the document, so the two live and die together.
    std::unique_ptr<GameDataDocument> _file;
    std::unique_ptr<GameDataResolver> _resolver;
    OriginalSlotLookup _originalOf;
    std::string _reason;
};

}  // namespace VoltMod
