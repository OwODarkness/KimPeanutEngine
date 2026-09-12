#include "live2d_model_instance.h"

#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

#include "Model/CubismMoc.hpp"
#include "CubismFramework.hpp"
#include "Id/CubismIdManager.hpp"
#include "Motion/ACubismMotion.hpp"
#include "Motion/CubismExpressionMotion.hpp"
#include "Motion/CubismExpressionMotionManager.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismMotionManager.hpp"
#include "Motion/CubismMotionQueueEntry.hpp"
#include "Motion/CubismMotionQueueManager.hpp"
#include "Type/csmVector.hpp"
#include "Model/CubismModel.hpp"
#include "Effect/CubismPose.hpp"
#include "live2d_cubism_lifecycle.h"
#include "live2d_model_resource.h"

namespace kpengine::live2d
{
    namespace
    {
        namespace Core = Live2D::Cubism::Core;

        using Live2D::Cubism::Framework::csmByte;
        using Live2D::Cubism::Framework::csmBlendMode;
        using Live2D::Cubism::Framework::csmFloat32;
        using Live2D::Cubism::Framework::csmInt32;
        using Live2D::Cubism::Framework::csmSizeInt;
        using Live2D::Cubism::Framework::csmVector;
        using Live2D::Cubism::Framework::csmUint32;
        using Live2D::Cubism::Framework::csmUint16;
        using Live2D::Cubism::Framework::ACubismMotion;
        using Live2D::Cubism::Framework::CubismExpressionMotion;
        using Live2D::Cubism::Framework::CubismExpressionMotionManager;
        using Live2D::Cubism::Framework::CubismFramework;
        using Live2D::Cubism::Framework::CubismIdHandle;
        using Live2D::Cubism::Framework::CubismMotion;
        using Live2D::Cubism::Framework::CubismMotionManager;
        using Live2D::Cubism::Framework::CubismMoc;
        using Live2D::Cubism::Framework::CubismModel;
        using Live2D::Cubism::Framework::CubismPose;

        using Live2D::Cubism::Core::csmAlphaBlendType_Over;
        using Live2D::Cubism::Core::csmColorBlendType_Add;
        using Live2D::Cubism::Core::csmColorBlendType_AddCompatible;
        using Live2D::Cubism::Core::csmColorBlendType_Multiply;
        using Live2D::Cubism::Core::csmColorBlendType_MultiplyCompatible;
        using Live2D::Cubism::Core::csmColorBlendType_Normal;

        bool IsCountRepresentable(const csmInt32 value) noexcept
        {
            return value >= 0 &&
                   static_cast<std::uint64_t>(value) <=
                       std::numeric_limits<std::uint32_t>::max();
        }

        bool MapBlendMode(const csmBlendMode &source,
                          Live2DBlendMode &destination) noexcept
        {
            if (source.GetAlphaBlendType() != csmAlphaBlendType_Over)
            {
                return false;
            }
            switch (source.GetColorBlendType())
            {
            case csmColorBlendType_Normal:
                destination = Live2DBlendMode::Normal;
                return true;
            case csmColorBlendType_Add:
            case csmColorBlendType_AddCompatible:
                destination = Live2DBlendMode::Additive;
                return true;
            case csmColorBlendType_Multiply:
            case csmColorBlendType_MultiplyCompatible:
                destination = Live2DBlendMode::Multiplicative;
                return true;
            default:
                return false;
            }
        }

        void HashBytes(std::uint64_t &hash, const void *bytes,
                       const std::size_t count) noexcept
        {
            constexpr std::uint64_t kPrime = 1099511628211ull;
            const auto *input = static_cast<const std::uint8_t *>(bytes);
            for (std::size_t index = 0u; index < count; ++index)
            {
                hash ^= input[index];
                hash *= kPrime;
            }
        }

        template <typename T>
        void HashValue(std::uint64_t &hash, const T &value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);
            HashBytes(hash, &value, sizeof(T));
        }

        struct CubismMotionDeleter final
        {
            void operator()(ACubismMotion *motion) const noexcept
            {
                if (motion != nullptr)
                {
                    ACubismMotion::Delete(motion);
                }
            }
        };

        struct CubismPoseDeleter final
        {
            void operator()(CubismPose *pose) const noexcept
            {
                if (pose != nullptr)
                {
                    CubismPose::Delete(pose);
                }
            }
        };

        bool IsSdkFloatTime(const double value) noexcept
        {
            return std::isfinite(value) && value >= 0.0 &&
                   value <= static_cast<double>(
                                 std::numeric_limits<csmFloat32>::max());
        }

        bool BuildEffectIds(const Live2DProductData &product,
                            const std::string_view group_name,
                            csmVector<CubismIdHandle> &ids,
                            std::string &diagnostic)
        {
            auto *id_manager = CubismFramework::GetIdManager();
            if (id_manager == nullptr)
            {
                diagnostic = "Live2D Cubism ID manager is unavailable";
                return false;
            }
            for (const Live2DParameterGroup &group : product.parameter_groups)
            {
                if (group.target != "Parameter" || group.name != group_name)
                {
                    continue;
                }
                for (const std::string &id : group.ids)
                {
                    const CubismIdHandle handle = id_manager->GetId(id.c_str());
                    if (handle == nullptr)
                    {
                        diagnostic = "Live2D parameter group ID could not be resolved: " + id;
                        return false;
                    }
                    ids.PushBack(handle);
                }
            }
            return true;
        }

        bool BuildStaticModelData(const CubismModel &model,
                                  const std::size_t texture_count,
                                  Live2DStaticModelData &out,
                                  std::string &diagnostic)
        {
            if (texture_count > std::numeric_limits<std::uint32_t>::max())
            {
                diagnostic = "Live2D texture dependency count exceeds the snapshot limit";
                return false;
            }
            Live2DStaticModelData data{};
            data.texture_count = static_cast<std::uint32_t>(texture_count);

            const Core::csmModel *core_model = model.GetModel();
            if (core_model == nullptr)
            {
                diagnostic = "Live2D Core model is null";
                return false;
            }

            Core::csmVector2 canvas_size{};
            Core::csmVector2 canvas_origin{};
            float pixels_per_unit = 0.0f;
            Core::csmReadCanvasInfo(core_model, &canvas_size, &canvas_origin,
                                    &pixels_per_unit);
            data.canvas.size_in_pixels = {canvas_size.X, canvas_size.Y};
            data.canvas.origin_in_pixels = {canvas_origin.X, canvas_origin.Y};
            data.canvas.pixels_per_unit = pixels_per_unit;

            const csmInt32 drawable_count = model.GetDrawableCount();
            if (!IsCountRepresentable(drawable_count))
            {
                diagnostic = "Live2D drawable count is invalid";
                return false;
            }
            data.feature_report.drawable_count =
                static_cast<std::uint32_t>(drawable_count);

            const csmInt32 offscreen_count = model.GetOffscreenCount();
            if (!IsCountRepresentable(offscreen_count))
            {
                diagnostic = "Live2D offscreen count is invalid";
                return false;
            }
            data.feature_report.offscreen_object_count =
                static_cast<std::uint32_t>(offscreen_count);
            data.feature_report.blend_group_count =
                model.IsBlendModeEnabled() ? 1u : 0u;

            const csmInt32 *mask_counts = model.GetDrawableMaskCounts();
            const csmInt32 **masks = model.GetDrawableMasks();
            const csmInt32 *render_orders = model.GetRenderOrders();
            if (drawable_count > 0 &&
                (mask_counts == nullptr || masks == nullptr ||
                 render_orders == nullptr))
            {
                diagnostic = "Live2D drawable metadata is incomplete";
                return false;
            }

            std::uint64_t topology_hash = 1469598103934665603ull;
            HashValue(topology_hash, data.canvas.size_in_pixels.x);
            HashValue(topology_hash, data.canvas.size_in_pixels.y);
            HashValue(topology_hash, data.canvas.origin_in_pixels.x);
            HashValue(topology_hash, data.canvas.origin_in_pixels.y);
            HashValue(topology_hash, data.canvas.pixels_per_unit);
            HashValue(topology_hash, data.texture_count);

            for (csmInt32 drawable_index = 0; drawable_index < drawable_count;
                 ++drawable_index)
            {
                const csmInt32 vertex_count =
                    model.GetDrawableVertexCount(drawable_index);
                const csmInt32 index_count =
                    model.GetDrawableVertexIndexCount(drawable_index);
                const csmInt32 texture_index =
                    model.GetDrawableTextureIndex(drawable_index);
                if (!IsCountRepresentable(vertex_count) ||
                    !IsCountRepresentable(index_count) || texture_index < 0 ||
                    static_cast<std::size_t>(texture_index) >= texture_count)
                {
                    data.feature_report.invalid_index_count++;
                    diagnostic = "Live2D drawable range or texture index is invalid at drawable " +
                                 std::to_string(drawable_index);
                    return false;
                }

                const auto vertex_count_u32 = static_cast<std::uint32_t>(vertex_count);
                const auto index_count_u32 = static_cast<std::uint32_t>(index_count);
                if (data.uvs.size() >
                        std::numeric_limits<std::uint32_t>::max() -
                            vertex_count_u32 ||
                    data.indices.size() >
                        std::numeric_limits<std::uint32_t>::max() -
                            index_count_u32)
                {
                    diagnostic = "Live2D concatenated geometry exceeds 32-bit ranges";
                    return false;
                }

                Live2DDrawableStatic drawable{};
                drawable.vertex_offset =
                    static_cast<std::uint32_t>(data.uvs.size());
                drawable.vertex_count = vertex_count_u32;
                drawable.first_index =
                    static_cast<std::uint32_t>(data.indices.size());
                drawable.index_count = index_count_u32;
                drawable.texture_index = static_cast<std::uint32_t>(texture_index);

                const Core::csmVector2 *uvs =
                    model.GetDrawableVertexUvs(drawable_index);
                const csmUint16 *indices =
                    model.GetDrawableVertexIndices(drawable_index);
                if ((vertex_count > 0 && uvs == nullptr) ||
                    (index_count > 0 && indices == nullptr))
                {
                    diagnostic = "Live2D drawable geometry data is null at drawable " +
                                 std::to_string(drawable_index);
                    return false;
                }
                for (csmInt32 vertex = 0; vertex < vertex_count; ++vertex)
                {
                    data.uvs.push_back({uvs[vertex].X, uvs[vertex].Y});
                    HashValue(topology_hash, uvs[vertex].X);
                    HashValue(topology_hash, uvs[vertex].Y);
                }
                for (csmInt32 index = 0; index < index_count; ++index)
                {
                    if (indices[index] >= vertex_count_u32)
                    {
                        data.feature_report.invalid_index_count++;
                        diagnostic = "Live2D drawable index is out of range at drawable " +
                                     std::to_string(drawable_index);
                        return false;
                    }
                    data.indices.push_back(indices[index]);
                    HashValue(topology_hash, indices[index]);
                }

                const csmInt32 mask_count = mask_counts[drawable_index];
                if (!IsCountRepresentable(mask_count) ||
                    (mask_count > 0 && masks[drawable_index] == nullptr))
                {
                    diagnostic = "Live2D mask metadata is invalid at drawable " +
                                 std::to_string(drawable_index);
                    return false;
                }
                for (csmInt32 mask = 0; mask < mask_count; ++mask)
                {
                    const csmInt32 source = masks[drawable_index][mask];
                    if (source < 0 || source >= drawable_count)
                    {
                        diagnostic = "Live2D mask source index is out of range at drawable " +
                                     std::to_string(drawable_index);
                        return false;
                    }
                    drawable.mask_source_drawable_indices.push_back(
                        static_cast<std::uint32_t>(source));
                    HashValue(topology_hash, source);
                }
                if (!drawable.mask_source_drawable_indices.empty())
                {
                    for (std::size_t context_index = 0u;
                         context_index < data.mask_contexts.size();
                         ++context_index)
                    {
                        if (data.mask_contexts[context_index]
                                .source_drawable_indices ==
                            drawable.mask_source_drawable_indices)
                        {
                            drawable.mask_context_index =
                                static_cast<std::uint32_t>(context_index);
                            break;
                        }
                    }
                    if (drawable.mask_context_index == kLive2DNoMaskContext)
                    {
                        drawable.mask_context_index =
                            static_cast<std::uint32_t>(data.mask_contexts.size());
                        data.mask_contexts.push_back(
                            {drawable.mask_source_drawable_indices});
                    }
                }

                // Counted per mode so a blend-coverage claim can be derived from
                // the loaded model. An unmapped mode is not coverage.
                Live2DBlendMode blend_mode = Live2DBlendMode::Normal;
                if (!MapBlendMode(model.GetDrawableBlendModeType(drawable_index),
                                  blend_mode))
                {
                    data.feature_report.unknown_blend_mode_count++;
                }
                else
                {
                    switch (blend_mode)
                    {
                    case Live2DBlendMode::Normal:
                        data.feature_report.normal_drawable_count++;
                        break;
                    case Live2DBlendMode::Additive:
                        data.feature_report.additive_drawable_count++;
                        break;
                    case Live2DBlendMode::Multiplicative:
                        data.feature_report.multiplicative_drawable_count++;
                        break;
                    }
                }
                data.drawables.push_back(std::move(drawable));
                HashValue(topology_hash, vertex_count_u32);
                HashValue(topology_hash, index_count_u32);
                HashValue(topology_hash, static_cast<std::uint32_t>(texture_index));
            }

            data.feature_report.active_mask_context_count =
                static_cast<std::uint32_t>(data.mask_contexts.size());
            data.maximum_position_bytes = data.uvs.size() * sizeof(Live2DVector2);
            data.topology_revision = topology_hash == 0u ? 1u : topology_hash;
            if (!ValidateLive2DStaticModelData(data, diagnostic))
            {
                return false;
            }
            out = std::move(data);
            return true;
        }

        bool IsIndexRepresentable(const std::size_t index) noexcept
        {
            return index <= static_cast<std::size_t>(
                                std::numeric_limits<csmInt32>::max());
        }
    }

    struct Live2DModelInstance::Impl final
    {
        using MotionPtr =
            std::unique_ptr<ACubismMotion, CubismMotionDeleter>;
        using PosePtr = std::unique_ptr<CubismPose, CubismPoseDeleter>;
        using QueueHandle =
            Live2D::Cubism::Framework::CubismMotionQueueEntryHandle;

        static constexpr std::size_t kMaxMotionEntries = 16u;
        static constexpr std::size_t kMaxExpressionEntries = 4u;
        static constexpr std::size_t kMaxPendingEvents = 64u;

        struct MotionClip final
        {
            std::string group;
            std::uint32_t index = 0u;
            MotionPtr motion;
        };

        struct ExpressionClip final
        {
            std::string name;
            MotionPtr expression;
        };

        enum class MotionState
        {
            Playing,
            Interrupting,
            Cancelling
        };

        struct MotionPlayback final
        {
            Live2DMotionKey key;
            Live2DPlaybackToken token{};
            QueueHandle handle{};
            MotionState state = MotionState::Playing;
        };

        std::optional<CubismModelInstanceLease> lease;
        CubismMoc *moc{};
        CubismModel *model{};
        PosePtr pose;
        Live2DStaticModelData static_data{};
        std::vector<MotionClip> motion_clips;
        std::vector<ExpressionClip> expression_clips;
        std::unique_ptr<CubismExpressionMotionManager> expression_manager;
        std::unique_ptr<CubismMotionManager> motion_manager;
        std::vector<MotionPlayback> motion_playbacks;
        std::vector<float> pending_parameter_values;
        std::vector<bool> pending_parameter_dirty;
        std::vector<Live2DPlaybackEvent> pending_events;
        std::vector<Live2DPlaybackEvent> callback_events;
        std::uint64_t instance_serial = 0u;
        std::uint64_t next_token_sequence = 1u;
        std::uint64_t frame_sequence = 0u;
        float playback_time_seconds = 0.0f;
        bool expression_active = false;
        bool expression_clearing = false;
        bool callback_overflow = false;
        std::string current_expression;

        ~Impl() noexcept
        {
            if (motion_manager != nullptr)
            {
                motion_manager->StopAllMotions();
            }
            if (expression_manager != nullptr)
            {
                expression_manager->StopAllMotions();
            }
            motion_manager.reset();
            expression_manager.reset();
            expression_clips.clear();
            motion_clips.clear();
            pose.reset();
            if (model != nullptr && moc != nullptr)
            {
                moc->DeleteModel(model);
                model = nullptr;
            }
            if (moc != nullptr)
            {
                CubismMoc::Delete(moc);
                moc = nullptr;
            }
            lease.reset();
        }

        bool IsValid() const noexcept
        {
            return lease.has_value() && lease->IsValid() && moc != nullptr &&
                   model != nullptr;
        }

        bool HasPlayback() const noexcept
        {
            return !motion_playbacks.empty() || expression_active ||
                   !pending_events.empty() ||
                   (expression_manager != nullptr &&
                    expression_manager->GetCubismMotionQueueEntries()->GetSize() != 0u);
        }

        void LoadParameterCheckpoint() noexcept
        {
            model->LoadParameters();
        }

        void ApplyPendingParameterWrites() noexcept
        {
            for (std::size_t index = 0u;
                 index < pending_parameter_dirty.size(); ++index)
            {
                if (pending_parameter_dirty[index])
                {
                    model->SetParameterValue(
                        static_cast<csmInt32>(index),
                        static_cast<csmFloat32>(pending_parameter_values[index]));
                }
            }
        }

        bool UpdatePrimaryMotion(const float delta_seconds) noexcept
        {
            return motion_manager->UpdateMotion(
                model, static_cast<csmFloat32>(delta_seconds));
        }

        void SavePrimaryCheckpoint() noexcept
        {
            model->SaveParameters();
        }

        void UpdateExpressionContribution(const float delta_seconds) noexcept
        {
            if (expression_active ||
                expression_manager->GetCubismMotionQueueEntries()->GetSize() != 0u)
            {
                expression_manager->UpdateMotion(
                    model, static_cast<csmFloat32>(delta_seconds));
            }
        }

        // L2D7 inserts ordered secondary contributors in this late-update slot.
        void ApplyLateUpdateContributors(const float delta_seconds) noexcept
        {
            if (pose != nullptr)
            {
                pose->UpdateParameters(
                    model, static_cast<csmFloat32>(delta_seconds));
            }
        }

        void UpdateModel() noexcept
        {
            model->Update();
        }

        static void CaptureMotionEvent(
            const Live2D::Cubism::Framework::CubismMotionQueueManager *,
            const Live2D::Cubism::Framework::csmString &, void *) noexcept;
    };

    void Live2DModelInstance::Impl::CaptureMotionEvent(
        const Live2D::Cubism::Framework::CubismMotionQueueManager *,
        const Live2D::Cubism::Framework::csmString &value,
        void *custom_data) noexcept
    {
        auto *impl = static_cast<Live2DModelInstance::Impl *>(custom_data);
        if (impl == nullptr ||
            impl->callback_events.size() >= Impl::kMaxPendingEvents)
        {
            if (impl != nullptr)
            {
                impl->callback_overflow = true;
            }
            return;
        }
        try
        {
            Live2DPlaybackEvent event{};
            event.kind = Live2DPlaybackEventKind::MotionUserEvent;
            event.value.assign(value.GetRawString());
            impl->callback_events.push_back(std::move(event));
        }
        catch (...)
        {
            impl->callback_overflow = true;
        }
    }
    namespace
    {
        bool TokensEqual(const Live2DPlaybackToken &left,
                         const Live2DPlaybackToken &right) noexcept
        {
            return left.instance_serial == right.instance_serial &&
                   left.sequence == right.sequence;
        }

        bool IsValidToken(const Live2DPlaybackToken &token) noexcept
        {
            return token.instance_serial != 0u && token.sequence != 0u;
        }

        bool AppendPlaybackEvent(std::vector<Live2DPlaybackEvent> &events,
                                 const Live2DPlaybackEventKind kind,
                                 const Live2DPlaybackToken token,
                                 const std::string_view value) noexcept
        {
            try
            {
                Live2DPlaybackEvent event{};
                event.kind = kind;
                event.token = token;
                if (!value.empty())
                {
                    event.value.assign(value.data(), value.size());
                }
                events.push_back(std::move(event));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }


    }

    Live2DModelInstance::Live2DModelInstance(
        std::shared_ptr<const Live2DModelResource> resource,
        std::vector<std::shared_ptr<const asset::TextureResource>>
            texture_dependencies,
        std::unique_ptr<Impl> impl) noexcept
        : resource_(std::move(resource))
        , texture_dependencies_(std::move(texture_dependencies))
        , impl_(std::move(impl))
    {
    }

    Live2DModelInstance::~Live2DModelInstance() noexcept = default;

    Live2DModelInstance::Live2DModelInstance(Live2DModelInstance &&) noexcept =
        default;

    Live2DModelInstance &Live2DModelInstance::operator=(
        Live2DModelInstance &&) noexcept = default;

    std::unique_ptr<Live2DModelInstance> Live2DModelInstance::Create(
        std::shared_ptr<const Live2DModelResource> resource,
        std::vector<std::shared_ptr<const asset::TextureResource>>
            texture_dependencies,
        const std::uint64_t instance_serial,
        CubismLifecycle &lifecycle)
    {
        if (resource == nullptr || instance_serial == 0u ||
            !lifecycle.IsInitialized() ||
            resource->Product().moc_bytes.empty() ||
            texture_dependencies.size() != resource->Product().textures.size())
        {
            return nullptr;
        }

        std::optional<CubismModelInstanceLease> lease =
            lifecycle.AcquireModelInstance();
        if (!lease.has_value())
        {
            return nullptr;
        }

        auto impl = std::make_unique<Impl>();
        impl->lease = std::move(lease);
        impl->instance_serial = instance_serial;
        const std::vector<std::byte> &moc_bytes = resource->Product().moc_bytes;
        impl->moc = CubismMoc::Create(
            reinterpret_cast<const csmByte *>(moc_bytes.data()),
            static_cast<csmSizeInt>(moc_bytes.size()), true);
        if (impl->moc == nullptr)
        {
            return nullptr;
        }

        impl->model = impl->moc->CreateModel();
        if (impl->model == nullptr)
        {
            return nullptr;
        }

        const Live2DProductData &product = resource->Product();
        for (const Live2DOptionalChunk &chunk : product.optional_chunks)
        {
            if (chunk.name != "Pose")
            {
                continue;
            }
            if (chunk.bytes.empty() ||
                chunk.bytes.size() >
                    static_cast<std::size_t>(std::numeric_limits<csmSizeInt>::max()))
            {
                return nullptr;
            }
            impl->pose.reset(CubismPose::Create(
                reinterpret_cast<const csmByte *>(chunk.bytes.data()),
                static_cast<csmSizeInt>(chunk.bytes.size())));
            if (impl->pose == nullptr)
            {
                return nullptr;
            }
            break;
        }

        std::string diagnostic;
        if (!BuildStaticModelData(*impl->model, texture_dependencies.size(),
                                  impl->static_data, diagnostic))
        {
            return nullptr;
        }
        if (!BuildClipLibrary(*resource, *impl, diagnostic))
        {
            return nullptr;
        }
        try
        {
            const std::size_t parameter_count =
                static_cast<std::size_t>(impl->model->GetParameterCount());
            impl->pending_parameter_values.assign(parameter_count, 0.0f);
            impl->pending_parameter_dirty.assign(parameter_count, false);
            impl->motion_playbacks.reserve(Impl::kMaxMotionEntries);
            impl->pending_events.reserve(Impl::kMaxPendingEvents);
            impl->callback_events.reserve(Impl::kMaxPendingEvents);
            impl->motion_manager->SetEventCallback(&Impl::CaptureMotionEvent, impl.get());
            impl->expression_manager->SetEventCallback(&Impl::CaptureMotionEvent, impl.get());
            if (impl->pose != nullptr)
            {
                impl->pose->UpdateParameters(impl->model, 0.0f);
            }
            impl->model->Update();
            impl->model->SaveParameters();
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D playback state initialization failed: ") +
                         error.what();
            return nullptr;
        }

        return std::unique_ptr<Live2DModelInstance>(new Live2DModelInstance(
            std::move(resource), std::move(texture_dependencies),
            std::move(impl)));
    }

    bool Live2DModelInstance::IsValid() const noexcept
    {
        return resource_ != nullptr && impl_ != nullptr && impl_->IsValid();
    }

    std::uint64_t Live2DModelInstance::InstanceSerial() const noexcept
    {
        return IsValid() ? impl_->instance_serial : 0u;
    }

    std::size_t Live2DModelInstance::MotionCount() const noexcept
    {
        return IsValid() ? impl_->motion_clips.size() : 0u;
    }

    bool Live2DModelInstance::HasMotion(const Live2DMotionKey &key) const noexcept
    {
        if (!IsValid())
        {
            return false;
        }
        for (const Impl::MotionClip &clip : impl_->motion_clips)
        {
            if (clip.group == key.group && clip.index == key.index)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t Live2DModelInstance::ExpressionCount() const noexcept
    {
        return IsValid() ? impl_->expression_clips.size() : 0u;
    }

    bool Live2DModelInstance::HasExpression(const std::string_view name) const noexcept
    {
        if (!IsValid())
        {
            return false;
        }
        for (const Impl::ExpressionClip &clip : impl_->expression_clips)
        {
            if (clip.name == name)
            {
                return true;
            }
        }
        return false;
    }

    bool Live2DModelInstance::BuildClipLibrary(
        const Live2DModelResource &resource,
        Impl &impl,
        std::string &diagnostic)
    {
        diagnostic.clear();
        const Live2DProductData &product = resource.Product();
        std::set<std::pair<std::string, std::uint32_t>> motion_keys;
        std::set<std::string> expression_names;
        csmVector<CubismIdHandle> eye_blink_ids;
        csmVector<CubismIdHandle> lip_sync_ids;
        if (!BuildEffectIds(product, "EyeBlink", eye_blink_ids, diagnostic) ||
            !BuildEffectIds(product, "LipSync", lip_sync_ids, diagnostic))
        {
            return false;
        }

        try
        {
            impl.motion_clips.reserve(product.motions.size());
            for (const Live2DAuthoredMotion &definition : product.motions)
            {
                if (definition.group.empty() ||
                    !motion_keys.insert({definition.group, definition.index}).second ||
                    definition.motion_bytes.empty() ||
                    definition.motion_bytes.size() >
                        static_cast<std::size_t>(
                            std::numeric_limits<csmSizeInt>::max()) ||
                    (definition.has_fade_in &&
                     !IsSdkFloatTime(definition.fade_in_time)) ||
                    (definition.has_fade_out &&
                     !IsSdkFloatTime(definition.fade_out_time)))
                {
                    diagnostic = "Live2D instance contains an invalid motion definition";
                    return false;
                }
                const csmByte *bytes =
                    reinterpret_cast<const csmByte *>(definition.motion_bytes.data());
                CubismMotion *motion = CubismMotion::Create(
                    bytes, static_cast<csmSizeInt>(definition.motion_bytes.size()),
                    nullptr, nullptr, true);
                if (motion == nullptr)
                {
                    diagnostic = "Live2D motion consistency validation failed for " +
                                 definition.group + "/" +
                                 std::to_string(definition.index);
                    return false;
                }
                Impl::MotionPtr owned_motion(motion);
                if (definition.has_fade_in)
                {
                    motion->SetFadeInTime(
                        static_cast<csmFloat32>(definition.fade_in_time));
                }
                if (definition.has_fade_out)
                {
                    motion->SetFadeOutTime(
                        static_cast<csmFloat32>(definition.fade_out_time));
                }
                motion->SetEffectIds(eye_blink_ids, lip_sync_ids);
                impl.motion_clips.push_back(
                    {definition.group, definition.index, std::move(owned_motion)});
            }

            impl.expression_clips.reserve(product.expressions.size());
            for (const Live2DAuthoredExpression &definition : product.expressions)
            {
                if (definition.name.empty() ||
                    !expression_names.insert(definition.name).second ||
                    definition.expression_bytes.empty() ||
                    definition.expression_bytes.size() >
                        static_cast<std::size_t>(
                            std::numeric_limits<csmSizeInt>::max()))
                {
                    diagnostic = "Live2D instance contains an invalid expression definition";
                    return false;
                }
                const csmByte *bytes =
                    reinterpret_cast<const csmByte *>(
                        definition.expression_bytes.data());
                CubismExpressionMotion *expression =
                    CubismExpressionMotion::Create(
                        bytes,
                        static_cast<csmSizeInt>(definition.expression_bytes.size()));
                if (expression == nullptr)
                {
                    diagnostic = "Live2D expression parsing failed for " + definition.name;
                    return false;
                }
                impl.expression_clips.push_back(
                    {definition.name, Impl::MotionPtr(expression)});
            }

            impl.expression_manager =
                std::make_unique<CubismExpressionMotionManager>();
            impl.motion_manager = std::make_unique<CubismMotionManager>();
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D clip library construction failed: ") +
                         error.what();
            return false;
        }
        return true;
    }

    const Live2DModelResource &Live2DModelInstance::Resource() const noexcept
    {
        return *resource_;
    }

    std::size_t Live2DModelInstance::ParameterCount() const noexcept
    {
        if (!IsValid())
        {
            return 0;
        }
        return static_cast<std::size_t>(impl_->model->GetParameterCount());
    }

    bool Live2DModelInstance::GetParameterRange(const std::size_t index,
                                                float &minimum,
                                                float &maximum) const noexcept
    {
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        const auto parameter_index = static_cast<csmUint32>(index);
        minimum = impl_->model->GetParameterMinimumValue(parameter_index);
        maximum = impl_->model->GetParameterMaximumValue(parameter_index);
        return true;
    }

    bool Live2DModelInstance::GetParameterValue(const std::size_t index,
                                                float &value) const noexcept
    {
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        value = impl_->model->GetParameterValue(static_cast<csmInt32>(index));
        return true;
    }

    bool Live2DModelInstance::SetParameterValue(const std::size_t index,
                                                const float value) noexcept
    {
        if (!IsValid() || !std::isfinite(value) || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        impl_->model->SetParameterValue(static_cast<csmInt32>(index),
                                         static_cast<csmFloat32>(value));
        impl_->pending_parameter_values[index] = value;
        impl_->pending_parameter_dirty[index] = true;
        return true;
    }

    bool Live2DModelInstance::Update() noexcept
    {
        if (!IsValid() || impl_->HasPlayback())
        {
            return false;
        }
        if (impl_->pose != nullptr)
        {
            impl_->pose->UpdateParameters(impl_->model, 0.0f);
        }
        impl_->model->Update();
        return true;
    }

    bool Live2DModelInstance::PlayMotion(
        const Live2DMotionKey &key, const std::int32_t priority,
        const Live2DMotionStartMode mode, Live2DPlaybackToken &token,
        std::string &diagnostic)
    {
        diagnostic.clear();
        token = {};
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        if (priority <= 0)
        {
            diagnostic = "Live2D motion priority must be positive";
            return false;
        }
        const Impl::MotionClip *clip = nullptr;
        for (const Impl::MotionClip &candidate : impl_->motion_clips)
        {
            if (candidate.group == key.group && candidate.index == key.index)
            {
                clip = &candidate;
                break;
            }
        }
        if (clip == nullptr)
        {
            diagnostic = resource_->Product().product_version < 2u
                             ? "Live2D Product V1 has no typed playback data; reimport required"
                             : "Live2D motion key was not found";
            return false;
        }
        if (impl_->motion_playbacks.size() >= Impl::kMaxMotionEntries ||
            impl_->pending_events.size() + impl_->motion_playbacks.size() + 1u >
                Impl::kMaxPendingEvents)
        {
            diagnostic = "Live2D motion transition or event capacity is exhausted";
            return false;
        }
        const csmInt32 current_priority =
            impl_->motion_manager->GetCurrentPriority();
        const csmInt32 reserved_priority =
            impl_->motion_manager->GetReservePriority();
        if (mode == Live2DMotionStartMode::RespectPriority &&
            (priority <= current_priority || priority <= reserved_priority))
        {
            diagnostic = "Live2D motion priority was rejected";
            return false;
        }
        if (impl_->next_token_sequence == 0u)
        {
            diagnostic = "Live2D motion token sequence is exhausted";
            return false;
        }

        const Live2DPlaybackToken new_token{impl_->instance_serial,
                                             impl_->next_token_sequence};
        const auto handle = impl_->motion_manager->StartMotionPriority(
            clip->motion.get(), false, static_cast<csmInt32>(priority));
        if (handle == Live2D::Cubism::Framework::
                         InvalidMotionQueueEntryHandleValue)
        {
            diagnostic = "Live2D SDK rejected the motion start";
            return false;
        }
        for (Impl::MotionPlayback &playback : impl_->motion_playbacks)
        {
            playback.state = Impl::MotionState::Interrupting;
        }
        impl_->motion_playbacks.push_back({key, new_token, handle,
                                            Impl::MotionState::Playing});
        if (impl_->next_token_sequence == std::numeric_limits<std::uint64_t>::max())
        {
            impl_->next_token_sequence = 0u;
        }
        else
        {
            ++impl_->next_token_sequence;
        }
        token = new_token;
        return true;
    }

    bool Live2DModelInstance::StopMotion(const Live2DPlaybackToken token,
                                          const Live2DStopMode mode,
                                          std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsValid() || !IsValidToken(token) ||
            token.instance_serial != impl_->instance_serial)
        {
            diagnostic = "Live2D motion token is invalid or belongs to another instance";
            return false;
        }
        std::size_t selected = impl_->motion_playbacks.size();
        for (std::size_t index = 0u; index < impl_->motion_playbacks.size(); ++index)
        {
            if (TokensEqual(impl_->motion_playbacks[index].token, token))
            {
                selected = index;
                break;
            }
        }
        if (selected == impl_->motion_playbacks.size())
        {
            diagnostic = "Live2D motion token is stale or already terminal";
            return false;
        }
        if (mode == Live2DStopMode::AuthoredFadeOut)
        {
            auto *entry = impl_->motion_manager->GetCubismMotionQueueEntry(
                impl_->motion_playbacks[selected].handle);
            if (entry == nullptr)
            {
                diagnostic = "Live2D motion token no longer has an SDK queue entry";
                return false;
            }
            entry->StartFadeout(
                entry->GetCubismMotion()->GetFadeOutTime(),
                impl_->playback_time_seconds);
            impl_->motion_playbacks[selected].state = Impl::MotionState::Cancelling;
            return true;
        }

        if (impl_->pending_events.size() >= Impl::kMaxPendingEvents)
        {
            diagnostic = "Live2D pending playback event capacity is exhausted";
            return false;
        }
        auto *entry = impl_->motion_manager->GetCubismMotionQueueEntry(
            impl_->motion_playbacks[selected].handle);
        if (entry == nullptr)
        {
            diagnostic = "Live2D motion token no longer has an SDK queue entry";
            return false;
        }
        if (!AppendPlaybackEvent(impl_->pending_events,
                                 Live2DPlaybackEventKind::MotionCancelled,
                                 impl_->motion_playbacks[selected].token, {}))
        {
            diagnostic = "Live2D pending playback event storage failed";
            return false;
        }
        // Mark only the selected queue entry finished. Replacing the whole
        // manager would incorrectly cancel other entries still fading out.
        entry->IsFinished(true);
        impl_->motion_playbacks.erase(
            impl_->motion_playbacks.begin() + static_cast<std::ptrdiff_t>(selected));
        return true;
    }

    void Live2DModelInstance::StopAllMotions(const Live2DStopMode mode) noexcept
    {
        if (!IsValid())
        {
            return;
        }
        if (mode == Live2DStopMode::AuthoredFadeOut)
        {
            for (Impl::MotionPlayback &playback : impl_->motion_playbacks)
            {
                auto *entry = impl_->motion_manager->GetCubismMotionQueueEntry(
                    playback.handle);
                if (entry != nullptr)
                {
                    entry->StartFadeout(
                        entry->GetCubismMotion()->GetFadeOutTime(),
                        impl_->playback_time_seconds);
                    playback.state = Impl::MotionState::Cancelling;
                }
            }
            return;
        }
        for (const Impl::MotionPlayback &playback : impl_->motion_playbacks)
        {
            AppendPlaybackEvent(impl_->pending_events,
                                Live2DPlaybackEventKind::MotionCancelled,
                                playback.token, {});
        }
        impl_->motion_manager->StopAllMotions();
        impl_->motion_playbacks.clear();
    }

    bool Live2DModelInstance::SetExpression(const std::string_view name,
                                             std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        const Impl::ExpressionClip *clip = nullptr;
        for (const Impl::ExpressionClip &candidate : impl_->expression_clips)
        {
            if (candidate.name == name)
            {
                clip = &candidate;
                break;
            }
        }
        if (clip == nullptr)
        {
            diagnostic = resource_->Product().product_version < 2u
                             ? "Live2D Product V1 has no typed playback data; reimport required"
                             : "Live2D expression name was not found";
            return false;
        }
        if (impl_->expression_manager->GetCubismMotionQueueEntries()->GetSize() >=
            Impl::kMaxExpressionEntries)
        {
            diagnostic = "Live2D expression transition capacity is exhausted";
            return false;
        }
        const auto handle = impl_->expression_manager->StartMotion(
            clip->expression.get(), false);
        if (handle == Live2D::Cubism::Framework::
                         InvalidMotionQueueEntryHandleValue)
        {
            diagnostic = "Live2D SDK rejected the expression start";
            return false;
        }
        impl_->expression_active = true;
        impl_->expression_clearing = false;
        impl_->current_expression.assign(name.data(), name.size());
        return true;
    }

    bool Live2DModelInstance::ClearExpression(const Live2DStopMode mode,
                                              std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        auto *entries = impl_->expression_manager->GetCubismMotionQueueEntries();
        if (mode == Live2DStopMode::Immediate)
        {
            impl_->expression_manager->StopAllMotions();
            impl_->expression_active = false;
            impl_->expression_clearing = false;
            impl_->current_expression.clear();
            return true;
        }
        for (csmUint32 index = 0u; index < entries->GetSize(); ++index)
        {
            if (entries->At(index) != nullptr)
            {
                entries->At(index)->StartFadeout(
                    entries->At(index)->GetCubismMotion()->GetFadeOutTime(),
                    impl_->playback_time_seconds);
            }
        }
        impl_->expression_clearing = true;
        impl_->current_expression.clear();
        return true;
    }

    bool Live2DModelInstance::AdvancePlayback(
        const float delta_seconds, Live2DPlaybackUpdateResult &result,
        std::string &diagnostic)
    {
        diagnostic.clear();
        result = {};
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f ||
            delta_seconds > std::numeric_limits<float>::max() -
                                impl_->playback_time_seconds)
        {
            diagnostic = "Live2D playback delta must be finite and non-negative";
            return false;
        }

        std::vector<Live2DPlaybackEvent> events = std::move(impl_->pending_events);
        impl_->pending_events.clear();
        impl_->callback_events.clear();
        impl_->callback_overflow = false;
        try
        {
            // These vectors are reserved at instance creation. Keep the event
            // budget explicit before entering SDK code that can invoke callbacks.
            events.reserve(Impl::kMaxPendingEvents);
            impl_->callback_events.reserve(Impl::kMaxPendingEvents);
            // Canonical transaction:
            // Load -> direct/base writes -> primary motion -> Save ->
            // expression -> late contributors -> model update.
            impl_->LoadParameterCheckpoint();
            impl_->ApplyPendingParameterWrites();
            impl_->playback_time_seconds += delta_seconds;
            const bool motion_updated =
                impl_->UpdatePrimaryMotion(delta_seconds);
            if (impl_->callback_overflow)
            {
                throw std::runtime_error("playback event storage failed");
            }
            impl_->SavePrimaryCheckpoint();
            impl_->UpdateExpressionContribution(delta_seconds);
            if (impl_->callback_overflow)
            {
                throw std::runtime_error("playback event storage failed");
            }
            impl_->ApplyLateUpdateContributors(delta_seconds);
            impl_->UpdateModel();

            for (Live2DPlaybackEvent &event : impl_->callback_events)
            {
                if (events.size() >= Impl::kMaxPendingEvents)
                {
                    throw std::runtime_error("playback event storage failed");
                }
                events.push_back(std::move(event));
            }
            impl_->callback_events.clear();
            auto *entries = impl_->motion_manager->GetCubismMotionQueueEntries();
            for (std::size_t index = 0u; index < impl_->motion_playbacks.size();)
            {
                bool present = false;
                for (csmUint32 entry_index = 0u; entry_index < entries->GetSize();
                     ++entry_index)
                {
                    if (entries->At(entry_index) != nullptr &&
                        static_cast<void *>(entries->At(entry_index)) ==
                            impl_->motion_playbacks[index].handle)
                    {
                        present = true;
                        break;
                    }
                }
                if (present)
                {
                    ++index;
                    continue;
                }
                const Impl::MotionPlayback &playback = impl_->motion_playbacks[index];
                const Live2DPlaybackEventKind kind =
                    playback.state == Impl::MotionState::Playing
                        ? Live2DPlaybackEventKind::MotionCompleted
                        : playback.state == Impl::MotionState::Interrupting
                              ? Live2DPlaybackEventKind::MotionInterrupted
                              : Live2DPlaybackEventKind::MotionCancelled;
                if (!AppendPlaybackEvent(events, kind, playback.token, {}))
                {
                    throw std::runtime_error("playback event storage failed");
                }
                impl_->motion_playbacks.erase(
                    impl_->motion_playbacks.begin() + static_cast<std::ptrdiff_t>(index));
            }

            if (impl_->expression_clearing &&
                impl_->expression_manager->GetCubismMotionQueueEntries()->GetSize() == 0u)
            {
                impl_->expression_active = false;
                impl_->expression_clearing = false;
            }
            for (std::size_t index = 0u; index < impl_->pending_parameter_dirty.size();
                 ++index)
            {
                impl_->pending_parameter_dirty[index] = false;
            }
            result.events = std::move(events);
            result.motion_parameters_updated = motion_updated;
            return true;
        }
        catch (const std::exception &error)
        {
            impl_->pending_events = std::move(events);
            diagnostic = std::string("Live2D playback update failed: ") + error.what();
            return false;
        }
    }

    const std::vector<std::shared_ptr<const asset::TextureResource>> &
    Live2DModelInstance::TextureDependencies() const noexcept
    {
        return texture_dependencies_;
    }

    bool Live2DModelInstance::ExtractStaticData(
        Live2DStaticModelData &out, std::string &diagnostic) const
    {
        diagnostic.clear();
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        try
        {
            Live2DStaticModelData copy = impl_->static_data;
            if (!ValidateLive2DStaticModelData(copy, diagnostic))
            {
                return false;
            }
            out = std::move(copy);
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D static extraction failed: ") +
                         error.what();
            return false;
        }
    }

    bool Live2DModelInstance::ExtractFrameSnapshot(
        Live2DFrameSnapshot &out, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!IsValid())
        {
            diagnostic = "Live2D model instance is invalid";
            return false;
        }
        if (impl_->frame_sequence == std::numeric_limits<std::uint64_t>::max())
        {
            diagnostic = "Live2D frame sequence exhausted";
            return false;
        }
        try
        {
            Live2DFrameSnapshot snapshot{};
            snapshot.topology_revision = impl_->static_data.topology_revision;
            snapshot.frame_sequence = impl_->frame_sequence + 1u;
            snapshot.positions.resize(
                impl_->static_data.maximum_position_bytes /
                sizeof(Live2DVector2));
            snapshot.drawables.resize(impl_->static_data.drawables.size());

            for (std::size_t drawable_index = 0u;
                 drawable_index < snapshot.drawables.size(); ++drawable_index)
            {
                const Live2DDrawableStatic &static_drawable =
                    impl_->static_data.drawables[drawable_index];
                const csmFloat32 *positions = impl_->model->GetDrawableVertices(
                    static_cast<csmInt32>(drawable_index));
                if (static_drawable.vertex_count > 0u && positions == nullptr)
                {
                    diagnostic = "Live2D drawable position data is null at drawable " +
                                 std::to_string(drawable_index);
                    return false;
                }
                for (std::uint32_t vertex = 0u;
                     vertex < static_drawable.vertex_count; ++vertex)
                {
                    snapshot.positions[static_drawable.vertex_offset + vertex] =
                        {positions[vertex * 2u], positions[vertex * 2u + 1u]};
                }

                Live2DDrawableState &state = snapshot.drawables[drawable_index];
                const Core::csmModel *core_model = impl_->model->GetModel();
                const auto *constant_flags =
                    Core::csmGetDrawableConstantFlags(core_model);
                const auto *dynamic_flags =
                    Core::csmGetDrawableDynamicFlags(core_model);
                const csmInt32 *render_orders = impl_->model->GetRenderOrders();
                if (constant_flags == nullptr || dynamic_flags == nullptr ||
                    render_orders == nullptr)
                {
                    diagnostic = "Live2D drawable state metadata is incomplete";
                    return false;
                }
                state.constant_flags =
                    static_cast<std::uint32_t>(constant_flags[drawable_index]);
                state.dynamic_flags =
                    static_cast<std::uint32_t>(dynamic_flags[drawable_index]);
                state.render_order = render_orders[drawable_index];
                state.opacity = impl_->model->GetDrawableOpacity(
                    static_cast<csmInt32>(drawable_index));
                state.visible = impl_->model->GetDrawableDynamicFlagIsVisible(
                    static_cast<csmInt32>(drawable_index));
                state.culling = impl_->model->GetDrawableCulling(
                                    static_cast<csmInt32>(drawable_index)) != 0;
                state.inverted_mask = impl_->model->GetDrawableInvertedMask(
                    static_cast<csmInt32>(drawable_index));
                if (!MapBlendMode(impl_->model->GetDrawableBlendModeType(
                                      static_cast<csmInt32>(drawable_index)),
                                  state.blend_mode))
                {
                    diagnostic = "Live2D drawable blend mode is unsupported at drawable " +
                                 std::to_string(drawable_index);
                    return false;
                }
                const Core::csmVector4 multiply = impl_->model->GetDrawableMultiplyColor(
                    static_cast<csmInt32>(drawable_index));
                const Core::csmVector4 screen = impl_->model->GetDrawableScreenColor(
                    static_cast<csmInt32>(drawable_index));
                state.multiply_color = {multiply.X, multiply.Y, multiply.Z,
                                        multiply.W};
                state.screen_color = {screen.X, screen.Y, screen.Z, screen.W};
            }

            if (!ValidateLive2DFrameSnapshot(impl_->static_data, snapshot,
                                             diagnostic))
            {
                return false;
            }
            out = std::move(snapshot);
            impl_->frame_sequence = out.frame_sequence;
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Live2D frame extraction failed: ") +
                         error.what();
            return false;
        }
    }
}
