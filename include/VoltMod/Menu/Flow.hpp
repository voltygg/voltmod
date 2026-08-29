#pragma once

#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <VoltMod/Menu/MenuPresets.hpp>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Multi-step menu wizard threading a state struct through its steps, for one player.
 *
 * A Flow owns one @p TState copy, opens each applicable step in order, re-runs the @ref Validate
 * check before every step AND before finishing (so "target left" / "permission revoked" abort
 * cleanly), renders an auto-built summary confirm dialog when configured, and finally hands the
 * accumulated state to @ref Finish. Every human-facing string is a value the caller has already
 * translated - the flow is built for one admin, so there is nothing left to resolve per slot -
 * and the @ref Validate error is a translation key, resolved in that player's language and
 * replied through `runtime.Policy.Reply`.
 *
 * @code
 * Flow<PendingPunishment>::Create(runtime.Menus, adminSlot, std::move(pending))
 *     ->Validate(StillPunishable)
 *     ->AddDurationStep({.Title = tr("punish.duration"),
 *                        .Presets = durations,
 *                        .Set = [](PendingPunishment& s, int sec) { s.DurationSec = sec; },
 *                        .CustomLabel = tr("punish.custom"),
 *                        .CustomPrompt = tr("punish.customPrompt"),
 *                        .Applies = [](const PendingPunishment& s) { return IsTimed(s.Type); }})
 *     ->Confirm({.Title = tr("punish.confirm"), .Summary = SummaryRows,
 *                .ConfirmLabel = tr("nav.confirm"), .CancelLabel = tr("nav.cancel")})
 *     ->Finish([](PendingPunishment& s) { Issue(s); })
 *     ->Start();
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
    /** Menu factory for a custom step; call `flow.Advance()` after mutating `flow.State()`. */
    using BuildFn = std::function<std::shared_ptr<Menu>(Flow& flow)>;
    /** Step predicate over the current state; a false skips the step. */
    using AppliesFn = std::function<bool(const TState&)>;

    /** A row per (label, seconds) preset, plus a chat-input row parsed by @ref ParseDuration when
     *  @ref CustomLabel is set. */
    struct DurationStep
    {
        std::string Title;
        std::vector<std::pair<std::string, int>> Presets;
        std::function<void(TState&, int seconds)> Set;
        /** Empty = no free-text row. */
        std::string CustomLabel;
        std::string CustomPrompt;
        AppliesFn Applies;
    };

    /**
     * A row per option, plus a free-text row (empty input re-prompts) when @ref CustomLabel is set.
     *
     * Each option is a (label, value) pair: the label is what the player reads, the value is the
     * caller's stable identity for the row (a preset code, say). @ref Set receives both, so a
     * state that stores the code and the display text separately does not have to recover one
     * from the other. When a row has no separate identity, pass the label as the value. The
     * custom row reports @ref CustomValue, defaulting to the typed text.
     */
    struct OptionsStep
    {
        std::string Title;
        std::vector<std::pair<std::string, std::string>> Options;
        std::function<void(TState&, const std::string& label, const std::string& value)> Set;
        std::string CustomLabel;
        std::string CustomPrompt;
        std::string CustomValue;
        AppliesFn Applies;
    };

    /** The summary dialog the flow ends with; rows render as "{label}: {value}". */
    struct ConfirmSpec
    {
        std::string Title;
        std::function<std::vector<std::pair<std::string, std::string>>(const TState&)> Summary;
        std::string ConfirmLabel;
        std::string CancelLabel;
    };

    /** @p menus opens every step for @p slot and closes them on abort or finish; it must outlive
     *  the flow, which one Load/Unload cycle guarantees. */
    static Ptr Create(MenuSession& menus, int slot, TState initial)
    {
        return Ptr(new Flow(menus, slot, std::move(initial)));
    }

    /** Append a custom step. */
    Ptr AddStep(BuildFn build, AppliesFn applies = {})
    {
        _steps.push_back({.Build = std::move(build), .Applies = std::move(applies)});
        return this->shared_from_this();
    }

    /** Append a duration-picker step, drawn by @ref VoltMod::BuildDurationMenu. */
    Ptr AddDurationStep(DurationStep step)
    {
        auto weak = this->weak_from_this();
        // Moved out, not copied: AddStep keeps the predicate on the Step, so leaving it on the
        // captured step too would hold a second copy for as long as the flow is alive.
        auto applies = std::move(step.Applies);
        return AddStep(
            [weak, step = std::move(step)](Flow&) -> std::shared_ptr<Menu> {
                auto self = weak.lock();
                if (!self)
                    return nullptr;

                return BuildDurationMenu({.Title = step.Title,
                                          .Presets = step.Presets,
                                          .Pick =
                                              [self, set = step.Set](int, int seconds) {
                                                  if (set)
                                                      set(self->_state, seconds);
                                                  self->Advance();
                                              },
                                          .CustomLabel = step.CustomLabel,
                                          .CustomPrompt = step.CustomPrompt});
            },
            std::move(applies));
    }

    /** Append an options step. */
    Ptr AddOptionsStep(OptionsStep step)
    {
        auto weak = this->weak_from_this();
        auto applies = std::move(step.Applies);
        return AddStep(
            [weak, step = std::move(step)](Flow&) -> std::shared_ptr<Menu> {
                auto self = weak.lock();
                return self ? self->BuildOptionsMenu(step) : nullptr;
            },
            std::move(applies));
    }

    /** Re-run before every step and before finish; return a translation key to abort (the key
     *  is resolved in the player's language, replied, and all their menus close). */
    Ptr Validate(std::function<std::optional<std::string>(const TState&)> check)
    {
        _validate = std::move(check);
        return this->shared_from_this();
    }

    /** End with a summary confirm dialog instead of finishing straight away. */
    Ptr Confirm(ConfirmSpec spec)
    {
        _confirm = std::move(spec);
        return this->shared_from_this();
    }

    Ptr Finish(std::function<void(TState&)> finish)
    {
        _finish = std::move(finish);
        return this->shared_from_this();
    }

    /** Open the first applicable step (or the confirm/finish when there are none). */
    void Start() { OpenFrom(0); }

    /** Move past the current step. Steps call this after writing their value into @ref State. */
    void Advance() { OpenFrom(_stepIndex + 1); }

    TState& State() { return _state; }

private:
    Flow(MenuSession& menus, int slot, TState initial) : _menus(&menus), _slot(slot), _state(std::move(initial)) {}

    struct Step
    {
        BuildFn Build;
        AppliesFn Applies;
    };

    std::shared_ptr<Menu> BuildOptionsMenu(const OptionsStep& step)
    {
        auto self = this->shared_from_this();
        MenuBuilder builder(step.Title);

        // One shared setter for the whole list: every row below stores its callback for as long as
        // the menu is open, so capturing step.Set by value would keep one copy per option.
        auto set = std::make_shared<const decltype(step.Set)>(step.Set);

        for (const auto& [label, value] : step.Options)
        {
            builder.Button(label, [self, set, label, value](int) {
                if (*set)
                    (*set)(self->_state, label, value);
                self->Advance();
            });
        }

        if (!step.CustomLabel.empty())
        {
            builder.Add(InputRow{.Label = step.CustomLabel,
                                 .Prompt = step.CustomPrompt,
                                 .Set = [self, set, customValue = step.CustomValue](int, std::string_view text) {
                                     std::string typed = Strings::Trim(std::string(text));
                                     if (typed.empty())
                                         return false;  // re-prompt
                                     if (*set)
                                         (*set)(self->_state, typed, customValue.empty() ? typed : customValue);
                                     self->Advance();
                                     return true;
                                 }});
        }

        return builder.Build();
    }

    /** The summary dialog, as @ref VoltMod::BuildConfirmMenu draws it. Cancel is left empty, so
     *  it closes through the session the dialog is drawn in - the one this flow opened on. */
    std::shared_ptr<Menu> BuildSummary()
    {
        auto self = this->shared_from_this();

        std::vector<std::string> lines;
        if (_confirm.Summary)
        {
            for (const auto& [label, value] : _confirm.Summary(_state))
                lines.push_back(value.empty() ? label : std::format("{}: {}", label, value));
        }

        return BuildConfirmMenu({.Title = _confirm.Title,
                                 .Lines = std::move(lines),
                                 .ConfirmLabel = _confirm.ConfirmLabel,
                                 .CancelLabel = _confirm.CancelLabel,
                                 .Confirm = [self](int) { self->RunFinish(); }});
    }

    void OpenFrom(std::size_t from)
    {
        if (!RunValidation())
            return;

        for (std::size_t i = from; i < _steps.size(); ++i)
        {
            if (_steps[i].Applies && !_steps[i].Applies(_state))
                continue;
            _stepIndex = i;
            if (auto menu = _steps[i].Build(*this))
                _menus->Open(_slot, std::move(menu));
            return;
        }

        if (_confirm.Summary)
            _menus->Open(_slot, BuildSummary());
        else
            RunFinish();
    }

    void RunFinish()
    {
        // Anything may have changed while the confirm dialog was up - validate one last time.
        if (!RunValidation())
            return;
        if (_finish)
            _finish(_state);
        _menus->CloseAll(_slot);
    }

    /** False = aborted (error replied, menus closed). */
    bool RunValidation()
    {
        if (!_validate)
            return true;
        auto error = _validate(_state);
        if (!error)
            return true;

        _menus->CloseAll(_slot, *error);
        return false;
    }

    MenuSession* _menus;
    int _slot;
    TState _state;
    std::vector<Step> _steps;
    std::size_t _stepIndex = 0;
    std::function<std::optional<std::string>(const TState&)> _validate;
    ConfirmSpec _confirm;
    std::function<void(TState&)> _finish;
};

}  // namespace VoltMod
