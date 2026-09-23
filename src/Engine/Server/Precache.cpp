#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Server/Precache.hpp>
#include <algorithm>

// The SDK declares this without defining it. The overloads are in Linux order; MSVC reverses them.
class IEntityResourceManifest
{
public:
    virtual void AddResource(const char*) = 0;
    virtual void AddResource(const char*, void*) = 0;
    virtual void AddResource(const char*, void*, void*, void*) = 0;
};

namespace VoltMod
{

void Precache::Add(std::string_view resourcePath)
{
    if (resourcePath.empty())
    {
        return;
    }

    if (std::ranges::find(_resources, resourcePath) != _resources.end())
    {
        return;
    }

    _resources.emplace_back(resourcePath);
}

void Precache::AddTo(IEntityResourceManifest& manifest) const
{
    for (const auto& path : _resources)
    {
        manifest.AddResource(path.c_str());
    }

    if (!_resources.empty())
    {
        Log::Info("Precache: added {} resource(s) to the session manifest.", _resources.size());
    }
}

}  // namespace VoltMod
