#include "Ui/LayoutPath.hpp"

#include <format>

namespace VoltMod
{

static constexpr std::string_view SourceExtension = ".xml";

Result<LayoutPath> LayoutPath::Parse(std::string_view layout)
{
    if (layout.empty())
        return std::unexpected(Error::Invalid("a layout name is required"));

    const bool isSourceXml = layout.ends_with(SourceExtension);

    if (!layout.contains('/'))
    {
        // Any extension but .xml is a compiled resource name or a typo; appending .xml cannot resolve it.
        if (layout.contains('.') && !isSourceXml)
            return std::unexpected(Error::Invalid(
                std::format("'{}' must name the layout's source .xml, not the compiled resource", layout)));

        return LayoutPath(std::format("{}{}{}", Directory, layout, isSourceXml ? "" : SourceExtension));
    }

    if (!layout.starts_with(Directory))
        return std::unexpected(Error::Invalid(std::format(
            "'{}' is outside {}, the only directory the addon whitelist allows layouts in", layout, Directory)));

    if (!isSourceXml)
        return std::unexpected(
            Error::Invalid(std::format("'{}' must name the layout's source .xml, not the compiled resource", layout)));

    return LayoutPath(std::string(layout));
}

std::string_view LayoutPath::Name() const noexcept
{
    std::string_view name = _resource;
    name.remove_prefix(name.rfind('/') + 1);
    name.remove_suffix(SourceExtension.size());
    return name;
}

}  // namespace VoltMod
