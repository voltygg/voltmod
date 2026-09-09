#pragma once

namespace VoltMod
{

/**
 * @brief One surface a menu session can be drawn on.
 *
 * @ref MenuManager owns one per surface and picks between them per player; the session itself
 * lives in @ref MenuCore, so nothing here keeps stacks, cursors or keys.
 */
class MenuRenderer
{
public:
    virtual ~MenuRenderer() = default;

    MenuRenderer(const MenuRenderer&) = delete;
    MenuRenderer& operator=(const MenuRenderer&) = delete;

    /** Claim @p slot for this surface, spawning whatever it draws on. False when it cannot. */
    virtual bool Attach(int slot) = 0;

    /** Draw the top of @p slot's stack. False when the surface is gone, which is the manager's
     *  cue to move the session to another one rather than draw nothing. */
    virtual bool Present(int slot) = 0;

    /** Take @p slot's menu off the screen and drop what it was drawn on. */
    virtual void Dismiss(int slot) = 0;

    /** Rows one page holds, which is also what the keyboard pages by. */
    [[nodiscard]] virtual int RowsPerPage() const = 0;

    /** Draw page @p page of the current menu next. A surface that shows every row ignores it. */
    virtual void ShowPage(int slot, int page) { (void)slot, (void)page; }

protected:
    MenuRenderer() = default;
};

}  // namespace VoltMod
