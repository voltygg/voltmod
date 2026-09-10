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

/** Runs a sequence of menus while collecting state for one player. */
template <class TState>
class Flow : public std::enable_shared_from_this<Flow<TState>>
{
public:
    using Ptr = std::shared_ptr<Flow>;
    /** Menu factory for a custom step; call `flow.Advance()` after mutating `flow.State()`. */
    using BuildFn = std::function<std::shared_ptr<Menu>(Flow& flow)>;
    /** Step predicate over the current state; a false skips the step. */
    using AppliesFn = std::function<bool(const TState&)>;

    /** Duration presets with optional chat input. */
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

    /** Labeled options with optional free-text input. */
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

    /** Optional summary shown before finishing. */
    struct ConfirmSpec
    {
        std::string Title;
        std::function<void(const TState& state, SummaryRows& rows)> Summary;
        std::string ConfirmLabel;
        std::string CancelLabel;
    };

    /** @p menus must outlive the flow. */
    static Ptr Create(MenuSurface& menus, int slot, TState initial)
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
        // Keep the predicate in Step instead of copying it into the callback.
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

    /** Runs before each step and finish. Return a translation key to abort. */
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
    Flow(MenuSurface& menus, int slot, TState initial) : _menus(&menus), _slot(slot), _state(std::move(initial)) {}

    struct Step
    {
        BuildFn Build;
        AppliesFn Applies;
    };

    std::shared_ptr<Menu> BuildOptionsMenu(const OptionsStep& step)
    {
        auto self = this->shared_from_this();
        MenuBuilder builder(step.Title);

        // Share one setter because each open row retains the callback.
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

    std::shared_ptr<Menu> BuildSummary()
    {
        auto self = this->shared_from_this();

        SummaryRows rows;
        if (_confirm.Summary)
            _confirm.Summary(_state, rows);

        // Resolved here because the flow knows which player it runs for.
        std::string confirmLabel =
            _confirm.ConfirmLabel.empty() ? _menus->Translate(_slot, "menu.confirm", "Confirm") : _confirm.ConfirmLabel;
        std::string cancelLabel =
            _confirm.CancelLabel.empty() ? _menus->Translate(_slot, "menu.cancel", "Cancel") : _confirm.CancelLabel;

        return BuildConfirmMenu({.Title = _confirm.Title,
                                 .Lines = std::move(rows).Take(),
                                 .ConfirmLabel = std::move(confirmLabel),
                                 .CancelLabel = std::move(cancelLabel),
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
            auto menu = _steps[i].Build(*this);
            if (!menu)
            {
                // A step that cannot build has nothing to show, so abort like a validation failure
                // instead of leaving the previous menu open.
                _menus->CloseAll(_slot, "menu.stepFailed");
                return;
            }
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
        // Check again after the confirmation dialog.
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

    MenuSurface* _menus;
    int _slot;
    TState _state;
    std::vector<Step> _steps;
    std::size_t _stepIndex = 0;
    std::function<std::optional<std::string>(const TState&)> _validate;
    ConfirmSpec _confirm;
    std::function<void(TState&)> _finish;
};

}  // namespace VoltMod
