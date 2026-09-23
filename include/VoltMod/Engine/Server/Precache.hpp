#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Custom resources (particles, models, sound events) added to every map's session manifest.
 *  A path takes effect from the next map load, and clients also need the file, such as through
 *  a workshop addon. */
class Precache
{
public:
    /** Queue @p resourcePath, such as "particles/foo.vpcf", once. */
    void Add(std::string_view resourcePath);

    /** Called by the framework while the engine builds a manifest. */
    void AddTo(IEntityResourceManifest& manifest) const;

private:
    std::vector<std::string> _resources;
};

}  // namespace VoltMod
