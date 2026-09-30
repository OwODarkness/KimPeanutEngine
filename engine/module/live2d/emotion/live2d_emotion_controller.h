#ifndef KPENGINE_LIVE2D_EMOTION_CONTROLLER_H
#define KPENGINE_LIVE2D_EMOTION_CONTROLLER_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kpengine::live2d
{
    struct Live2DEmotionIntent final
    {
        std::string name;
    };

    struct Live2DEmotionMotionCandidate final
    {
        std::string group;
        std::uint32_t index = 0u;
        std::uint32_t priority = 0u;
    };

    struct Live2DEmotionPlan final
    {
        std::string name;
        std::vector<std::string> expression_candidates;
        std::vector<Live2DEmotionMotionCandidate> motion_candidates;
        std::string bubble_text;
    };

    using Live2DEmotionDecision =
        std::function<std::optional<Live2DEmotionPlan>(const Live2DEmotionIntent &)>;

    struct Live2DEmotionActions final
    {
        std::function<void(std::string &)> clear_expression;
        std::function<bool(std::string_view, std::string &)> set_expression;
        std::function<bool(const Live2DEmotionMotionCandidate &, std::string &)>
            start_motion;
        std::function<bool(std::string_view, std::string &)> show_bubble;
    };

    // UI-free semantic seam. The default decision is deterministic and table-driven;
    // a later model can be injected without changing editor or renderer code.
    class Live2DEmotionController final
    {
    public:
        Live2DEmotionController();
        explicit Live2DEmotionController(Live2DEmotionDecision decision);

        std::optional<Live2DEmotionPlan> Resolve(std::string_view intent) const;

    private:
        Live2DEmotionDecision decision_;
    };

    // Applies a semantic plan through narrow callbacks. Renderer and UI types stay
    // outside this folder, leaving the seam ready for a decision-model provider.
    class Live2DEmotionOrchestrator final
    {
    public:
        Live2DEmotionOrchestrator() = default;
        explicit Live2DEmotionOrchestrator(Live2DEmotionDecision decision);

        bool Apply(std::string_view intent, const Live2DEmotionActions &actions);

        const std::string &Status() const noexcept { return status_; }
        const std::string &Diagnostic() const noexcept { return diagnostic_; }

    private:
        Live2DEmotionController controller_;
        std::string status_ = "Normal";
        std::string diagnostic_;
    };
}

#endif // KPENGINE_LIVE2D_EMOTION_CONTROLLER_H
