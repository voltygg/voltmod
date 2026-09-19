#pragma once

#include <string>

namespace VoltMod
{

/** A value and the text a player sees for it, such as one entry of a choice list. */
template <class T>
struct Labeled
{
    std::string Label;
    T Value;
};

}  // namespace VoltMod
