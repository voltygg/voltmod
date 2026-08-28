#include "Ui/LayoutName.hpp"

#include <format>

namespace VoltMod
{

Result<std::string> ResolveLayoutName(std::string_view layout)
{
    if (layout.empty())
        return std::unexpected(Error::Invalid("a layout name is required"));

    const bool isSourceXml = layout.ends_with(".xml");

    if (layout.find('/') == std::string_view::npos)
    {
        // A bare name is `welcome` or `welcome.xml`. Any other extension is a compiled resource
        // name or a typo, and appending `.xml` to it would build a path that cannot resolve.
        if (layout.contains('.') && !isSourceXml)
            return std::unexpected(Error::Invalid(
                std::format("'{}' must name the layout's source .xml, not the compiled resource", layout)));

        return std::format("{}{}{}", kLayoutRoot, layout, isSourceXml ? "" : ".xml");
    }

    if (!layout.starts_with(kLayoutRoot))
        return std::unexpected(Error::Invalid(std::format(
            "'{}' is outside {}, the only directory the addon whitelist allows layouts in", layout, kLayoutRoot)));

    if (!isSourceXml)
        return std::unexpected(
            Error::Invalid(std::format("'{}' must name the layout's source .xml, not the compiled resource", layout)));

    return std::string(layout);
}

}  // namespace VoltMod
