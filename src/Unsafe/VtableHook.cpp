#include <VoltMod/Unsafe/VtableHook.hpp>
#include <utility>

namespace VoltMod
{

VtableHook::~VtableHook()
{
    Reset();
}

VtableHook::VtableHook(VtableHook&& other) noexcept
    : _preId(std::exchange(other._preId, 0)),
      _postId(std::exchange(other._postId, 0)),
      _remove(std::exchange(other._remove, nullptr))
{}

VtableHook& VtableHook::operator=(VtableHook&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        _preId = std::exchange(other._preId, 0);
        _postId = std::exchange(other._postId, 0);
        _remove = std::exchange(other._remove, nullptr);
    }
    return *this;
}

void VtableHook::Reset() noexcept
{
    // SourceHook removes by id and does not dereference the hooked object.
    if (_remove)
    {
        if (_preId != 0)
            _remove(_preId);
        if (_postId != 0)
            _remove(_postId);
    }
    _preId = 0;
    _postId = 0;
    _remove = nullptr;
}

Result<VtableHook> VtableHook::Install(std::string_view what, bool wantPre, bool wantPost,
                                       const std::function<int(bool post)>& add, VtableHookRemover remove)
{
    if (!add || !remove || (!wantPre && !wantPost))
        return std::unexpected(Error::Invalid("VtableHook::Install needs an adder, a remover and a side to add"));

    VtableHook hook;
    hook._remove = remove;

    if (wantPre)
        hook._preId = add(false);
    // Do not add the post hook when its pre hook was refused.
    if (wantPost && (!wantPre || hook._preId != 0))
        hook._postId = add(true);

    const char* refused = nullptr;
    if (wantPre && hook._preId == 0)
        refused = "pre";
    else if (wantPost && hook._postId == 0)
        refused = "post";

    if (refused)
    {
        // Install or remove the pair together; handlers assume both halves exist.
        hook.Reset();
        return std::unexpected(Error::Engine(std::format("SourceHook refused the {} {} hook", what, refused)));
    }

    return hook;
}

}  // namespace VoltMod
