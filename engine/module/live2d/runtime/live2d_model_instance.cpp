#include "live2d_model_instance.h"

#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"
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
        using Live2D::Cubism::Framework::csmUint32;
        using Live2D::Cubism::Framework::csmUint16;
        using Live2D::Cubism::Framework::CubismMoc;
        using Live2D::Cubism::Framework::CubismModel;

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
        std::optional<CubismModelInstanceLease> lease;
        CubismMoc *moc{};
        CubismModel *model{};
        Live2DStaticModelData static_data{};
        std::uint64_t frame_sequence = 0u;

        ~Impl() noexcept
        {
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
    };

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
        CubismLifecycle &lifecycle)
    {
        if (resource == nullptr || !lifecycle.IsInitialized() ||
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

        std::string diagnostic;
        if (!BuildStaticModelData(*impl->model, texture_dependencies.size(),
                                  impl->static_data, diagnostic))
        {
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
        if (!IsValid() || !IsIndexRepresentable(index) ||
            index >= ParameterCount())
        {
            return false;
        }
        impl_->model->SetParameterValue(static_cast<csmInt32>(index),
                                         static_cast<csmFloat32>(value));
        return true;
    }

    bool Live2DModelInstance::Update() noexcept
    {
        if (!IsValid())
        {
            return false;
        }
        impl_->model->Update();
        return true;
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
