#include "ui_control_validation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace kpengine::editor::ui::detail
{
    namespace
    {
        bool IsNumeric(const UiValue &value)
        {
            return std::holds_alternative<std::int64_t>(value) ||
                   std::holds_alternative<std::uint64_t>(value) ||
                   std::holds_alternative<double>(value);
        }

        bool RangeIsValid(const UiControl &control)
        {
            if (!IsNumeric(control.value))
                return false;
            const std::size_t type = control.value.index();
            if ((control.minimum && control.minimum->index() != type) ||
                (control.maximum && control.maximum->index() != type) ||
                (control.step && control.step->index() != type))
                return false;

            return std::visit(
                [&control](const auto &value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::int64_t> ||
                                  std::is_same_v<T, std::uint64_t> ||
                                  std::is_same_v<T, double>)
                    {
                        if (control.minimum && value < std::get<T>(*control.minimum))
                            return false;
                        if (control.maximum && value > std::get<T>(*control.maximum))
                            return false;
                        if (control.minimum && control.maximum &&
                            std::get<T>(*control.minimum) > std::get<T>(*control.maximum))
                            return false;
                        if (control.step && std::get<T>(*control.step) < T{})
                            return false;
                        if constexpr (std::is_same_v<T, double>)
                        {
                            if (!std::isfinite(value) ||
                                (control.minimum && !std::isfinite(std::get<T>(*control.minimum))) ||
                                (control.maximum && !std::isfinite(std::get<T>(*control.maximum))) ||
                                (control.step && !std::isfinite(std::get<T>(*control.step))))
                                return false;
                        }
                        return true;
                    }
                    return false;
                }, control.value);
        }

        bool HasAction(const UiControl &control, const UiActionKind action)
        {
            return std::find(control.actions.begin(), control.actions.end(), action) !=
                   control.actions.end();
        }

        void Require(const bool condition, const char *message)
        {
            if (!condition)
                throw std::logic_error(message);
        }

        void ValidateButtonInput(const UiControl &control)
        {
            Require(std::holds_alternative<bool>(control.value),
                    "button controls require a bool result value");
            Require(HasAction(control, UiActionKind::Activate),
                    "button controls must declare the activate action");
        }

        void ValidateCheckboxInput(const UiControl &control)
        {
            Require(std::holds_alternative<bool>(control.value),
                    "checkbox controls require a bool value");
            Require(HasAction(control, UiActionKind::Toggle),
                    "checkbox controls must declare the toggle action");
        }

        void ValidateSelectableInput(const UiControl &control)
        {
            Require(std::holds_alternative<bool>(control.value),
                    "selectable controls require a bool value");
            Require(HasAction(control, UiActionKind::Select),
                    "selectable controls must declare the select action");
        }

        void ValidateNumericInput(const UiControl &control)
        {
            Require(RangeIsValid(control), "numeric control value or range is invalid");
            Require(HasAction(control, UiActionKind::Edit) &&
                        HasAction(control, UiActionKind::Commit),
                    "numeric controls must declare edit and commit actions");
        }

        void ValidateSliderInput(const UiControl &control)
        {
            Require(control.minimum.has_value() && control.maximum.has_value(),
                    "slider controls require inclusive minimum and maximum values");
            ValidateNumericInput(control);
        }

        void ValidateTextInput(const UiControl &control)
        {
            Require(std::holds_alternative<std::string>(control.value),
                    "text controls require a string value");
            Require(HasAction(control, UiActionKind::Edit) &&
                        HasAction(control, UiActionKind::Commit) &&
                        HasAction(control, UiActionKind::Cancel),
                    "text controls must declare edit, commit and cancel actions");
        }

        void ValidateComboInput(const UiControl &control)
        {
            Require(!control.choices.empty(), "combo controls require at least one choice");
            Require(HasAction(control, UiActionKind::Select),
                    "combo controls must declare the select action");
            std::unordered_set<std::string> keys;
            for (const UiChoice &choice : control.choices)
            {
                Require(!choice.key.empty() && choice.value.index() == control.value.index(),
                        "combo choices require stable keys and matching value types");
                Require(keys.insert(choice.key).second, "combo choice keys must be unique");
            }
        }

        void ValidateInputForRole(const UiControl &control)
        {
            switch (control.role)
            {
            case UiRole::Button: ValidateButtonInput(control); break;
            case UiRole::CheckBox: ValidateCheckboxInput(control); break;
            case UiRole::Selectable: ValidateSelectableInput(control); break;
            case UiRole::NumericInput: ValidateNumericInput(control); break;
            case UiRole::Slider: ValidateSliderInput(control); break;
            case UiRole::TextInput: ValidateTextInput(control); break;
            case UiRole::Combo: ValidateComboInput(control); break;
            case UiRole::Window:
            case UiRole::Generic:
            case UiRole::Group:
            case UiRole::Image:
            case UiRole::Canvas: break;
            }
        }

        void ValidateResultAction(const UiControl &control, const UiEditResult &result)
        {
            if (!result.action)
            {
                Require(!result.changed && !result.committed && !result.cancelled &&
                            !result.activated,
                        "UI backend returned interaction state without an action");
                return;
            }
            Require(control.enabled, "disabled controls cannot produce actions");
            Require(HasAction(control, *result.action),
                    "UI backend returned an action the control did not declare");
        }

        void ValidateButtonResult(const UiControl &control, const UiEditResult &result)
        {
            Require(std::holds_alternative<bool>(result.value),
                    "button result value must be bool");
            Require(result.value == control.value && !result.changed && !result.committed &&
                        !result.cancelled,
                    "button results cannot mutate the button value");
            if (result.activated)
                Require(result.action == UiActionKind::Activate,
                        "activated buttons must return the activate action");
            else
                Require(!result.action, "inactive buttons cannot return an action");
        }

        void ValidateCheckboxResult(const UiControl &control, const UiEditResult &result)
        {
            Require(std::holds_alternative<bool>(result.value),
                    "checkbox result value must be bool");
            const bool changed = result.value != control.value;
            Require(result.changed == changed && result.committed == changed &&
                        !result.cancelled && !result.activated,
                    "checkbox result flags must match its toggle value");
            Require(result.action.has_value() == changed &&
                        (!changed || result.action == UiActionKind::Toggle),
                    "checkbox toggle action must match its changed value");
        }

        void ValidateSelectableResult(const UiControl &control, const UiEditResult &result)
        {
            Require(std::holds_alternative<bool>(result.value),
                    "selectable result value must be bool");
            if (result.activated)
            {
                Require(result.action == UiActionKind::Select && std::get<bool>(result.value) &&
                            result.committed,
                        "activated selectables must commit a selected state");
            }
            else
            {
                Require(!result.action && !result.changed && !result.committed &&
                            result.value == control.value,
                        "inactive selectables cannot change state");
            }
        }

        void ValidateNumericResult(const UiControl &control, const UiEditResult &result)
        {
            UiControl candidate = control;
            candidate.value = result.value;
            Require(RangeIsValid(candidate), "numeric result violates its value/range contract");
            Require(result.changed == (result.value != control.value),
                    "numeric changed flag must match the returned value");
            if (result.changed)
                Require(result.action == UiActionKind::Edit ||
                            result.action == UiActionKind::Commit,
                        "changed numeric results require edit or commit action");
            if (result.committed)
                Require(result.action == UiActionKind::Commit,
                        "committed numeric results require commit action");
            Require(!result.cancelled && !result.activated,
                    "numeric controls cannot return cancel or activate state");
        }

        void ValidateTextResult(const UiControl &control, const UiEditResult &result)
        {
            Require(std::holds_alternative<std::string>(result.value),
                    "text result value must be a string");
            Require(result.changed == (result.value != control.value) || result.cancelled,
                    "text changed flag must match the returned value");
            if (result.cancelled)
            {
                Require(result.value == control.value && !result.changed &&
                            !result.committed && result.action == UiActionKind::Cancel,
                        "cancelled text results must restore the original value");
            }
            else if (result.committed)
            {
                Require(result.action == UiActionKind::Commit,
                        "committed text results require commit action");
            }
            else if (result.changed)
            {
                Require(result.action == UiActionKind::Edit,
                        "changed text results require edit action");
            }
            else
            {
                Require(!result.action, "unchanged text results cannot return an action");
            }
            Require(!result.activated, "text inputs cannot return activate state");
        }

        void ValidateComboResult(const UiControl &control, const UiEditResult &result)
        {
            Require(result.value.index() == control.value.index(),
                    "combo backend returned a value with the wrong type");
            Require(result.changed == (result.value != control.value),
                    "combo changed flag must match the returned value");
            const bool value_is_choice = std::any_of(
                control.choices.begin(), control.choices.end(),
                [&result](const UiChoice &choice) { return choice.value == result.value; });
            if (result.activated || result.changed || result.committed)
                Require(result.action == UiActionKind::Select && value_is_choice,
                        "combo selection results require select action");
            else
                Require(!result.action, "unchanged combos cannot return an action");
        }

        void ValidateResultForRole(const UiControl &control, const UiEditResult &result)
        {
            switch (control.role)
            {
            case UiRole::Button: ValidateButtonResult(control, result); break;
            case UiRole::CheckBox: ValidateCheckboxResult(control, result); break;
            case UiRole::Selectable: ValidateSelectableResult(control, result); break;
            case UiRole::NumericInput: ValidateNumericResult(control, result); break;
            case UiRole::Slider: ValidateNumericResult(control, result); break;
            case UiRole::TextInput: ValidateTextResult(control, result); break;
            case UiRole::Combo: ValidateComboResult(control, result); break;
            case UiRole::Window:
            case UiRole::Generic:
            case UiRole::Group:
            case UiRole::Image:
            case UiRole::Canvas:
                Require(result.value.index() == control.value.index(),
                        "UI backend returned a value with the wrong type");
                break;
            }
            ValidateResultAction(control, result);
        }
    }

    void ValidateControlInput(const UiControl &control)
    {
        if (!std::isfinite(control.width) || control.width < 0.0F ||
            !std::isfinite(control.height) || control.height < 0.0F)
            throw std::invalid_argument("UI control size must be finite and non-negative");
        ValidateInputForRole(control);
    }

    void ValidateControlResult(const UiControl &control, const UiEditResult &result)
    {
        ValidateResultForRole(control, result);
    }

    void ValidateCanvas(const UiVec2 size,
                        const std::vector<UiCanvasPrimitive> &primitives)
    {
        Require(std::isfinite(size.x) && std::isfinite(size.y) &&
                    size.x >= 0.0F && size.y >= 0.0F,
                "canvas extent must be finite and non-negative");
        for (const UiCanvasPrimitive &primitive : primitives)
        {
            const auto finite_point = [](const UiVec2 point)
            {
                return std::isfinite(point.x) && std::isfinite(point.y);
            };
            Require(finite_point(primitive.first) && finite_point(primitive.second),
                    "canvas primitive coordinates must be finite");
            Require(std::isfinite(primitive.color.r) && std::isfinite(primitive.color.g) &&
                        std::isfinite(primitive.color.b) && std::isfinite(primitive.color.a) &&
                        primitive.color.r >= 0.0F && primitive.color.r <= 1.0F &&
                        primitive.color.g >= 0.0F && primitive.color.g <= 1.0F &&
                        primitive.color.b >= 0.0F && primitive.color.b <= 1.0F &&
                        primitive.color.a >= 0.0F && primitive.color.a <= 1.0F,
                    "canvas colors must be finite normalized RGBA values");
            Require(std::isfinite(primitive.thickness) && primitive.thickness > 0.0F &&
                        std::isfinite(primitive.radius) && primitive.radius >= 0.0F,
                    "canvas stroke width and radius must be finite and non-negative");
            if (primitive.kind == UiCanvasPrimitiveKind::Rect ||
                primitive.kind == UiCanvasPrimitiveKind::FilledRect)
                Require(primitive.second.x >= primitive.first.x &&
                            primitive.second.y >= primitive.first.y,
                        "canvas rectangle corners must be ordered");
            if (primitive.kind == UiCanvasPrimitiveKind::Text)
                Require(primitive.text.size() <= 8192 && finite_point(primitive.text_anchor) &&
                            std::isfinite(primitive.text_scale) && primitive.text_scale > 0.0F,
                        "canvas text exceeds its bounded length");
            if (primitive.kind == UiCanvasPrimitiveKind::Image)
                Require(primitive.image != 0 &&
                            primitive.second.x >= primitive.first.x &&
                            primitive.second.y >= primitive.first.y,
                        "canvas image requires an ID and ordered bounds");
        }
    }
}
