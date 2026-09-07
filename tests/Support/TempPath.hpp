#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace VoltModTests
{

/** A name under the system temp directory unique to this path, this process, and this tag. Every
 *  doctest case is its own CTest entry and those run in parallel, so the counter alone - which
 *  restarts at zero in each process - is not enough to keep two runs off the same file. */
inline std::filesystem::path UniqueTempPath(std::string_view tag)
{
    static const auto seed = std::random_device{}();
    static int counter = 0;
    return std::filesystem::temp_directory_path() / std::format("voltmod-{}-{:x}-{}", tag, seed, ++counter);
}

/** A temporary file holding @p text, removed again when the test ends. */
class TempFile
{
public:
    /** @p tag names the test that owns the file and @p extension includes its dot. */
    TempFile(std::string_view text, std::string_view tag, std::string_view extension)
    {
        _path = UniqueTempPath(tag);
        _path += extension;
        std::ofstream out(_path, std::ios::binary);
        out << text;
    }

    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string Path() const { return _path.string(); }

private:
    std::filesystem::path _path;
};

/** A temporary directory the test writes files into, removed with its contents when it ends. */
class TempDir
{
public:
    explicit TempDir(std::string_view tag) : _path(UniqueTempPath(tag)) { std::filesystem::create_directories(_path); }

    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(_path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /** Write @p text to @p name inside the directory, overwriting it. */
    void Write(std::string_view name, std::string_view text) const
    {
        std::ofstream out(_path / name, std::ios::binary);
        out << text;
    }

    std::string Path() const { return _path.string(); }

private:
    std::filesystem::path _path;
};

}  // namespace VoltModTests
