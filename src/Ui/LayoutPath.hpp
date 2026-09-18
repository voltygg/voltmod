#pragma once

#include <VoltMod/Core/Results/Result.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A layout resource the client will accept, checked before anything is spawned.
 *
 * `welcome` and `welcome.xml` both become `panorama/layout/custom_game/welcome.xml`; a full path
 * must already be under @ref Directory and name the source `.xml`. A bad name renders nothing and
 * says so only on the client console, so it is refused here instead. SDK-free for its tests.
 */
class LayoutPath
{
public:
    /** The only directory gameinfo.gi's addon whitelist allows Panorama layouts in. */
    static constexpr std::string_view Directory = "panorama/layout/custom_game/";

    static Result<LayoutPath> Parse(std::string_view layout);

    /** The resource name that goes on the entity. */
    [[nodiscard]] const std::string& Resource() const noexcept { return _resource; }

    /** The file name without directory or extension: the layout's root element id. */
    [[nodiscard]] std::string_view Name() const noexcept;

private:
    explicit LayoutPath(std::string resource) : _resource(std::move(resource)) {}

    std::string _resource;
};

}  // namespace VoltMod
