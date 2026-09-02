#ifndef KPENGINE_RUNTIME_RENDER_RENDER_PASS_H
#define KPENGINE_RUNTIME_RENDER_RENDER_PASS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kpengine::render
{
    enum class RenderPassResource : uint8_t
    {
        SceneColor,
        SceneHdr,
        GBuffer,
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        CaptureOutput,
        Count,
    };

    enum class RenderPassAccess : uint8_t
    {
        Read,
        Write,
    };

    enum class FixedRenderPassId : uint8_t
    {
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        GBuffer,
        DeferredLighting,
        ToneMap,
        CaptureView,
        EditorComposite,
        Count,
    };

    enum class RenderPassExecutionOwner : uint8_t
    {
        Renderer,
        External,
    };

    enum class RenderPassCondition : uint8_t
    {
        Always,
        DiagnosticCaptureRequested,
        ExternalRequest,
    };

    enum class RenderPassOutcome : uint8_t
    {
        Pending,
        Executed,
        SkippedCondition,
        SkippedExternal,
        Failed,
    };

    struct RenderPassResourceUse
    {
        RenderPassResource resource = RenderPassResource::SceneColor;
        RenderPassAccess access = RenderPassAccess::Read;
    };

    struct FixedRenderPassEntry
    {
        FixedRenderPassId id = FixedRenderPassId::DirectionalShadow;
        std::string name;
        std::vector<RenderPassResourceUse> resources;
        RenderPassExecutionOwner owner = RenderPassExecutionOwner::Renderer;
        RenderPassCondition condition = RenderPassCondition::Always;
        bool terminal = false;
    };

    class FixedRenderPassSequence final
    {
    public:
        static std::optional<FixedRenderPassSequence> Create(
            std::vector<FixedRenderPassEntry> entries, std::string &error);

        FixedRenderPassSequence(FixedRenderPassSequence &&other) noexcept;
        FixedRenderPassSequence &operator=(FixedRenderPassSequence &&other) noexcept;
        FixedRenderPassSequence(const FixedRenderPassSequence &) = delete;
        FixedRenderPassSequence &operator=(const FixedRenderPassSequence &) = delete;

        const std::vector<FixedRenderPassEntry> &Entries() const noexcept { return entries_; }
        bool IsValid() const noexcept
        {
            return entries_.size() == static_cast<std::size_t>(FixedRenderPassId::Count);
        }

    private:
        explicit FixedRenderPassSequence(std::vector<FixedRenderPassEntry> entries)
            : entries_(std::move(entries))
        {
        }

        std::vector<FixedRenderPassEntry> entries_;
    };

    class FixedRenderPassFrame final
    {
    public:
        FixedRenderPassFrame(const FixedRenderPassSequence &sequence,
                             bool diagnostic_capture_requested);
        ~FixedRenderPassFrame() = default;
        FixedRenderPassFrame(const FixedRenderPassFrame &) = delete;
        FixedRenderPassFrame &operator=(const FixedRenderPassFrame &) = delete;
        FixedRenderPassFrame(FixedRenderPassFrame &&other) noexcept;
        FixedRenderPassFrame &operator=(FixedRenderPassFrame &&other) noexcept;

        bool ExecuteRenderer(const std::function<bool(FixedRenderPassId)> &executor);
        bool ExecuteExternal(const std::function<void()> &executor);
        bool Finalize(std::string &error);

        RenderPassOutcome GetOutcome(FixedRenderPassId id) const noexcept;
        bool HasRequiredFailure() const noexcept { return required_failure_; }
        bool IsFinalized() const noexcept { return finalized_; }

    private:
        static constexpr std::size_t kPassCount =
            static_cast<std::size_t>(FixedRenderPassId::Count);

        bool IsValidId(FixedRenderPassId id) const noexcept;

        const FixedRenderPassSequence *sequence_ = nullptr;
        std::array<RenderPassOutcome, kPassCount> outcomes_{};
        bool diagnostic_capture_requested_ = false;
        bool renderer_executed_ = false;
        bool external_executed_ = false;
        bool finalized_ = false;
        bool required_failure_ = false;
        std::size_t cursor_ = 0;
    };
}

#endif
