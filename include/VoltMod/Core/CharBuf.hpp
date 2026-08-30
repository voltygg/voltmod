#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace VoltMod
{

template <size_t N>
struct FixedString
{
    char Value[N]{};

    constexpr FixedString(const char (&text)[N]) { std::copy_n(text, N, Value); }

    [[nodiscard]] constexpr std::string_view View() const { return {Value, N - 1}; }
};

/**
 * @brief A value for a fixed engine character array (`char m_name[N]`).
 *
 * Assignment truncates to N-1 characters, adds a NUL, and clears the unused tail. @ref View
 * borrows this buffer; @ref Str returns a copy.
 */
template <size_t N>
struct CharBuf
{
    static_assert(N > 1, "a CharBuf needs room for at least one character and a NUL");

    char Value[N]{};

    CharBuf() = default;
    CharBuf(std::string_view text) noexcept { Assign(text); }

    /** Supports direct construction from a literal without two user-defined conversions. */
    CharBuf(const char* text) noexcept { Assign(text ? std::string_view(text) : std::string_view{}); }

    CharBuf& operator=(std::string_view text) noexcept
    {
        Assign(text);
        return *this;
    }

    /** Avoids ambiguous assignment from a string literal. */
    CharBuf& operator=(const char* text) noexcept
    {
        Assign(text ? std::string_view(text) : std::string_view{});
        return *this;
    }

    /** Contents before the first NUL, borrowed from this buffer. */
    [[nodiscard]] std::string_view View() const noexcept
    {
        size_t length = 0;
        while (length < N && Value[length] != '\0')
            ++length;
        return {Value, length};
    }

    [[nodiscard]] std::string Str() const { return std::string(View()); }

    [[nodiscard]] bool Empty() const noexcept { return Value[0] == '\0'; }

    void Assign(std::string_view text) noexcept
    {
        const size_t length = std::min(text.size(), N - 1);
        std::memset(Value, 0, N);
        if (length > 0)
            std::memcpy(Value, text.data(), length);
    }
};

}  // namespace VoltMod
