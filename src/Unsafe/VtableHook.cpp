#include <VoltMod/Unsafe/VtableHook.hpp>
#include <utility>

namespace VoltMod
{

// SDK-free on purpose: everything that has to name a SourceHook macro lives in the
// VOLTMOD_VHOOK traits, so the install policy below is compiled straight into the unit tests.

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
    // Removal is by id: SourceHook resolves it from what it recorded at add time and never
    // dereferences the hooked object, so this is safe even after a map change has destroyed
    // every instance the hook was bound to.
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
    // A post that brackets a pre which was never added has nothing to bracket, so do not spend
    // the add at all once pre came back refused.
    if (wantPost && (!wantPre || hook._preId != 0))
        hook._postId = add(true);

    const char* refused = nullptr;
    if (wantPre && hook._preId == 0)
        refused = "pre";
    else if (wantPost && hook._postId == 0)
        refused = "post";

    if (refused)
    {
        // Pair-or-nothing: a surviving half would run against state its counterpart never
        // established, and the handler cannot see that the other side is missing.
        hook.Reset();
        return std::unexpected(Error::Engine(std::format("SourceHook refused the {} {} hook", what, refused)));
    }

    return hook;
}

}  // namespace VoltMod
