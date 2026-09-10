#pragma once

#include <VoltMod/Core/Result.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace VoltModTests
{

/** Records what a widget wrote instead of driving a real panel. */
struct FakePanel
{
    std::vector<std::tuple<int, std::string, std::string, std::string>> Texts;
    std::vector<std::tuple<int, std::string, std::string, bool>> Classes;

    VoltMod::Status SetText(int slot, std::string_view id, std::string_view var, std::string_view value)
    {
        Texts.emplace_back(slot, std::string(id), std::string(var), std::string(value));
        return {};
    }

    VoltMod::Status SetClass(int slot, std::string_view id, std::string_view cls, bool on)
    {
        Classes.emplace_back(slot, std::string(id), std::string(cls), on);
        return {};
    }

    /** The class writes that turned something on, in order. */
    [[nodiscard]] std::vector<std::string> Enabled() const
    {
        std::vector<std::string> found;
        for (const auto& [slot, id, cls, on] : Classes)
            if (on)
                found.push_back(id + "." + cls);
        return found;
    }
};

}  // namespace VoltModTests
