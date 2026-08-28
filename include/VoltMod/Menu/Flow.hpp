#pragma once

// A traditional guard macro alongside `#pragma once` - VoltMod otherwise uses `#pragma once`
// alone - purely so a translation unit that never meant to include this header can detect
// that it did. tests/Api/RootApiSurfaceTest.cpp asserts <VoltMod/Api.hpp> never defines this.
#ifndef VOLTMOD_MENU_FLOW_HPP
#define VOLTMOD_MENU_FLOW_HPP
#endif

#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Menu/Html/HtmlMenuManager.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Multi-step menu wizard threading a state struct through its steps.
 *
 * A Flow owns one @p TState copy, opens each applicable step in order, re-runs the OnValidate
 * check before every step AND before finishing (so "target left" / "permission revoked" abort
 * cleanly), renders an auto-built summary confirm dialog when configured, and finally hands the
 * accumulated state to OnFinish. Human-facing text comes from caller-supplied per-slot providers,
 * so the framework carries no localization; the OnValidate error is a translation key (resolved in the
 * offending player's language, replied via runtime.Policy.Reply).
 *
 * @code
 * Flow<PendingPunishment>::Create(runtime.HtmlMenus, std::move(pending))
 *     ->OnValidate(ValidateTargetStillPunishable)
 *     ->AddDurationStep(title, durationPresets, [](auto& s, int sec) { s.DurationSec = sec; },
 *                       customLabel, customPrompt, [](const auto& s) { return IsTimed(s.Type); })
 *     ->AddOptionsStep(title, reasonPresets, [](auto& s, std::string r) { s.Reason = std::move(r); },
 *                      customLabel, customPrompt)
 *     ->WithConfirm(confirmTitle, SummaryRows, confirmLabel, cancelLabel)
 *     ->OnFinish(IssueFromState)
 *     ->Start(adminSlot);
 * @endcode
 *
 * Lifetime: the open menus' row callbacks hold the only shared_ptr references, so the flow
 * lives exactly as long as one of its menus is on screen (steps store weak references).
 */
template <class TState>
class Flow : public std::enable_shared_from_this<Flow<TState>>
{
public:
    using Ptr = std::shared_ptr<Flow>;
    /** Per-slot text provider (title, button label, prompt, ...). */
    using LabelFn = std::function<std::string(int slot)>;
    /** Menu factory for a custom step; call `flow.Advance(slot)` after mutating `flow.State()`. */
    using BuildFn = std::function<std::shared_ptr<MenuView>(int slot, Flow& flow)>;
    /** Step predicate over the current state; a false skips the step. */
    using AppliesFn = std::function<bool(const TState&)>;

    /** @p menus opens every step and closes them on abort or finish; it must outlive the flow,
     *  which one Load/Unload cycle guarantees. */
    static Ptr Create(HtmlMenuManager& menus, TState initial) { return Ptr(new Flow(menus, std::move(initial))); }

    /** Append a custom step. */
    Ptr AddStep(BuildFn build, AppliesFn applies = {})
    {
        _steps.push_back({std::move(build), std::move(applies)});
        return this->shared_from_this();
    }

    /** Append a duration-picker step: rows from @p presets (label, seconds) pairs, plus an
     *  optional custom chat-input row when @p customLabel is set. */
    Ptr AddDurationStep(LabelFn title, std::function<std::vector<std::pair<std::string, int>>(int slot)> presets,
                        std::function<void(TState&, int seconds)> set, LabelFn customLabel = {},
                        LabelFn customPrompt = {}, AppliesFn applies = {})
    {
        auto weak = this->weak_from_this();
        return AddStep(
            [weak, title = std::move(title), presets = std::move(presets), set = std::move(set),
             customLabel = std::move(customLabel),
             customPrompt = std::move(customPrompt)](int slot, Flow&) -> std::shared_ptr<MenuView> {
                auto self = weak.lock();
                if (!self)
                    return nullptr;
                auto onPick = [self, set](int s, int seconds) {
                    set(self->_state, seconds);
                    self->Advance(s);
                };
                return BuildDurationPicker(slot, title(slot), presets(slot), std::move(onPick),
                                           customLabel ? customLabel(slot) : "",
                                           customPrompt ? customPrompt(slot) : "");
            },
            std::move(applies));
    }

    /**
     * Append an options step: one button per option, plus an optional free-text input row
     * (empty input re-prompts) when @p customLabel is set.
     *
     * Each option is a (label, value) pair, like @ref AddDurationStep - the label is shown and
     * localized, the value is the caller's stable identity for the row (a preset code, say).
     * `set` receives both, so a state that stores the code and the display text separately does
     * not have to recover one from the other. When a row has no separate identity, pass the
     * label as the value. The custom-input row reports @p customValue as its value, defaulting
     * to the typed text; a @p customLabel that resolves to an empty string omits the row.
     */
    using Option = std::pair<std::string, std::string>;  // (label, value)

    Ptr AddOptionsStep(LabelFn title, std::function<std::vector<Option>(int slot)> options,
                       std::function<void(TState&, const std::string& label, const std::string& value)> set,
                       LabelFn customLabel = {}, LabelFn customPrompt = {}, std::string customValue = {},
                       AppliesFn applies = {})
    {
        auto weak = this->weak_from_this();
        return AddStep(
            [weak, title = std::move(title), options = std::move(options), set = std::move(set),
             customLabel = std::move(customLabel), customPrompt = std::move(customPrompt),
             customValue = std::move(customValue)](int slot, Flow&) -> std::shared_ptr<MenuView> {
                auto self = weak.lock();
                if (!self)
                    return nullptr;

                MenuBuilder builder(title(slot));
                for (const auto& [label, value] : options(slot))
                {
                    builder.Button(label, [self, set, label, value](int s) {
                        set(self->_state, label, value);
                        self->Advance(s);
                    });
                }

                // An empty label means "no custom row", matching BuildDurationPicker - so a
                // caller can gate the row on config without splitting the chain.
                const std::string custom = customLabel ? customLabel(slot) : std::string();
                if (!custom.empty())
                {
                    builder.Input(
                        custom, customPrompt ? customPrompt(slot) : "", [](int) { return std::string(); },
                        [self, set, customValue](int s, std::string_view text) {
                            std::string typed = Strings::Trim(std::string(text));
                            if (typed.empty())
                                return false;  // re-prompt
                            set(self->_state, typed, customValue.empty() ? typed : customValue);
                            self->Advance(s);
                            return true;
                        },
                        64);
                }
                return builder.Build();
            },
            std::move(applies));
    }

    /** Re-run before every step and before finish; return a translation key to abort (the key
     *  is resolved in the player's language, replied, and all their menus close). */
    Ptr OnValidate(std::function<std::optional<std::string>(int slot, const TState&)> check)
    {
        _validate = std::move(check);
        return this->shared_from_this();
    }

    /** End with a summary confirm dialog; rows render as "{label}: {value}". */
    Ptr WithConfirm(LabelFn title,
                    std::function<std::vector<std::pair<std::string, std::string>>(int slot, const TState&)> summary,
                    LabelFn confirmLabel, LabelFn cancelLabel)
    {
        _confirmTitle = std::move(title);
        _confirmSummary = std::move(summary);
        _confirmLabel = std::move(confirmLabel);
        _cancelLabel = std::move(cancelLabel);
        return this->shared_from_this();
    }

    Ptr OnFinish(std::function<void(int slot, TState&)> finish)
    {
        _finish = std::move(finish);
        return this->shared_from_this();
    }

    /** Open the first applicable step (or the confirm/finish when there are none). */
    void Start(int slot) { OpenFrom(slot, 0); }

    /** Move past the current step. Steps call this after writing their value into State(). */
    void Advance(int slot) { OpenFrom(slot, _stepIndex + 1); }

    TState& State() { return _state; }

private:
    Flow(HtmlMenuManager& menus, TState initial) : _menus(&menus), _state(std::move(initial)) {}

    struct Step
    {
        BuildFn Build;
        AppliesFn Applies;
    };

    void OpenFrom(int slot, std::size_t from)
    {
        if (!RunValidation(slot))
            return;

        for (std::size_t i = from; i < _steps.size(); ++i)
        {
            if (_steps[i].Applies && !_steps[i].Applies(_state))
                continue;
            _stepIndex = i;
            if (auto menu = _steps[i].Build(slot, *this))
                _menus->OpenMenu(slot, menu);
            return;
        }

        if (_confirmSummary)
            OpenConfirm(slot);
        else
            RunFinish(slot);
    }

    void OpenConfirm(int slot)
    {
        auto self = this->shared_from_this();
        ConfirmDialogSpec spec{
            .Title = _confirmTitle ? _confirmTitle(slot) : std::string{},
            .ConfirmLabel = _confirmLabel ? _confirmLabel(slot) : std::string{},
            .CancelLabel = _cancelLabel ? _cancelLabel(slot) : std::string{},
            .OnConfirm = [self](int s) { self->RunFinish(s); },
        };
        for (const auto& [label, value] : _confirmSummary(slot, _state))
            spec.BodyLines.push_back(value.empty() ? label : std::format("{}: {}", label, value));

        _menus->OpenMenu(slot, BuildConfirmDialog(*_menus, std::move(spec)));
    }

    void RunFinish(int slot)
    {
        // Anything may have changed while the confirm dialog was up - validate one last time.
        if (!RunValidation(slot))
            return;
        if (_finish)
            _finish(slot, _state);
        _menus->CloseAllMenus(slot);
    }

    /** False = aborted (error replied, menus closed). */
    bool RunValidation(int slot)
    {
        if (!_validate)
            return true;
        auto error = _validate(slot, _state);
        if (!error)
            return true;

        _menus->CloseAllWithReply(slot, *error);
        return false;
    }

    HtmlMenuManager* _menus;
    TState _state;
    std::vector<Step> _steps;
    std::size_t _stepIndex = 0;
    std::function<std::optional<std::string>(int, const TState&)> _validate;
    std::function<std::vector<std::pair<std::string, std::string>>(int, const TState&)> _confirmSummary;
    LabelFn _confirmTitle;
    LabelFn _confirmLabel;
    LabelFn _cancelLabel;
    std::function<void(int, TState&)> _finish;
};

}  // namespace VoltMod
