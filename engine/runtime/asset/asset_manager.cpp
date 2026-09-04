#include "asset_manager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include "assimp_model_loader.h"
#include "image_io/image_io.h"
#include "shader_program_loader.h"
#include "miniaudio_audio_loader.h"
#include "material_loader.h"
#include "level_loader.h"
#include "utility.h"
#include "model.h"
#include "mesh.h"
#include "texture.h"
#include "audio.h"
#include "asset_load_observation_internal.h"
#include "log/logger.h"

namespace kpengine::asset
{
    namespace
    {
        constexpr uint32_t kMaxImportedHdrWidth = 4096;

        uint64_t ElapsedMicroseconds(
            std::chrono::steady_clock::time_point begin,
            std::chrono::steady_clock::time_point end) noexcept
        {
            if (end <= begin)
            {
                return 0;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin);
            return elapsed.count() < 0 ? 0 : static_cast<uint64_t>(elapsed.count());
        }

        const char *PhaseName(AssetLoadPhase phase) noexcept
        {
            switch (phase)
            {
            case AssetLoadPhase::CacheLookup:
                return "cache lookup";
            case AssetLoadPhase::WaitingForLoader:
                return "loader wait";
            case AssetLoadPhase::LoadSource:
                return "source load";
            case AssetLoadPhase::ResolveDependencies:
                return "dependency resolution";
            case AssetLoadPhase::Register:
                return "registration";
            }
            return "unknown phase";
        }

        std::string DisplayPath(const std::string &path)
        {
            try
            {
                std::string normalized =
                    std::filesystem::path(path).lexically_normal().generic_string();
                while (normalized.size() > 1 && normalized.back() == '/')
                {
                    normalized.pop_back();
                }
                const std::string asset_root =
                    std::filesystem::path(GetAssetDirectory()).lexically_normal().generic_string();
                const std::string normalized_key = CanonicalAssetPathKey(normalized);
                std::string root_key = CanonicalAssetPathKey(asset_root);
                while (root_key.size() > 1 && root_key.back() == '/')
                {
                    root_key.pop_back();
                }
                if (normalized_key == root_key ||
                    (normalized_key.size() > root_key.size() &&
                     normalized_key.compare(0, root_key.size(), root_key) == 0 &&
                     normalized_key[root_key.size()] == '/'))
                {
                    if (normalized_key == root_key)
                    {
                        return "<asset directory>";
                    }
                    return normalized.substr(root_key.size() + 1);
                }

                const std::string filename = std::filesystem::path(normalized).filename().generic_string();
                return filename.empty() ? "<asset directory>" : filename;
            }
            catch (...)
            {
                return "<asset path unavailable>";
            }
        }

        std::optional<uint64_t> ProbeSourceFileSize(const std::string &path) noexcept
        {
            try
            {
                std::error_code error;
                const uintmax_t size = std::filesystem::file_size(path, error);
                if (error || size > std::numeric_limits<uint64_t>::max())
                {
                    return std::nullopt;
                }
                return static_cast<uint64_t>(size);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        bool CheckedAdd(uint64_t &total, uint64_t value) noexcept
        {
            if (std::numeric_limits<uint64_t>::max() - total < value)
            {
                return false;
            }
            total += value;
            return true;
        }

        bool CheckedMultiply(uint64_t left, uint64_t right, uint64_t &result) noexcept
        {
            if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
            {
                return false;
            }
            result = left * right;
            return true;
        }

        std::optional<uint64_t> MeshPayloadSize(const MeshPtr &mesh) noexcept
        {
            if (!mesh || !mesh->data)
            {
                return std::nullopt;
            }

            uint64_t total = 0;
            uint64_t bytes = 0;
            if (!CheckedMultiply(static_cast<uint64_t>(mesh->data->vertices.size()),
                                 static_cast<uint64_t>(sizeof(Vertex)), bytes) ||
                !CheckedAdd(total, bytes) ||
                !CheckedMultiply(static_cast<uint64_t>(mesh->data->indices.size()),
                                 static_cast<uint64_t>(sizeof(uint32_t)), bytes) ||
                !CheckedAdd(total, bytes) ||
                !CheckedMultiply(static_cast<uint64_t>(mesh->data->sections.size()),
                                 static_cast<uint64_t>(sizeof(MeshSection)), bytes) ||
                !CheckedAdd(total, bytes))
            {
                return std::nullopt;
            }
            return total;
        }

        std::optional<uint64_t> MeasureDecodedPayload(
            AssetManager &manager,
            const AssetRegisterInfo &info) noexcept
        {
            if (const TexturePtr *texture = std::get_if<TexturePtr>(&info.resource))
            {
                if (!*texture || !(*texture)->data ||
                    (*texture)->data->pixels.size() > std::numeric_limits<uint64_t>::max())
                {
                    return std::nullopt;
                }
                return static_cast<uint64_t>((*texture)->data->pixels.size());
            }
            if (const AudioPtr *audio = std::get_if<AudioPtr>(&info.resource))
            {
                if (!*audio || !(*audio)->data)
                {
                    return std::nullopt;
                }
                uint64_t bytes = 0;
                return CheckedMultiply(static_cast<uint64_t>((*audio)->data->pcm.size()),
                                       static_cast<uint64_t>(sizeof(float)), bytes)
                           ? std::optional<uint64_t>(bytes)
                           : std::nullopt;
            }
            if (const MeshPtr *mesh = std::get_if<MeshPtr>(&info.resource))
            {
                return MeshPayloadSize(*mesh);
            }
            if (const ModelPtr *model = std::get_if<ModelPtr>(&info.resource))
            {
                if (!*model)
                {
                    return std::nullopt;
                }
                uint64_t total = 0;
                for (const AssetID &dependency : info.dependencies)
                {
                    if (dependency.type != AssetType::KPAT_Mesh)
                    {
                        continue;
                    }
                    const MeshPtr mesh = manager.GetResource<MeshResource>(dependency);
                    const std::optional<uint64_t> mesh_bytes = MeshPayloadSize(mesh);
                    if (!mesh_bytes || !CheckedAdd(total, *mesh_bytes))
                    {
                        return std::nullopt;
                    }
                }
                return total;
            }
            return std::nullopt;
        }

        class ObservedOperation
        {
        public:
            ObservedOperation(
                std::shared_ptr<detail::AssetLoadSessionState> state,
                AssetLoadOperationID operation)
                : state_(std::move(state)), operation_(operation)
            {
            }

            ~ObservedOperation() noexcept
            {
                if (IsActive())
                {
                    Finish(AssetLoadState::Failed, std::nullopt, std::nullopt,
                           "observation ended before the load reached a terminal state");
                }
            }

            bool IsActive() const noexcept
            {
                return state_ != nullptr && operation_ != 0 && !finished_;
            }

            void SetPhase(AssetLoadPhase phase) noexcept
            {
                if (IsActive())
                {
                    state_->UpdatePhase(operation_, phase);
                }
            }

            void SetTiming(const AssetLoadTiming &timing) noexcept
            {
                if (timing.cache_lookup_us != 0)
                {
                    timing_.cache_lookup_us = timing.cache_lookup_us;
                }
                if (timing.loader_queue_wait_us != 0)
                {
                    timing_.loader_queue_wait_us = timing.loader_queue_wait_us;
                }
                if (timing.source_load_us != 0)
                {
                    timing_.source_load_us = timing.source_load_us;
                }
                if (timing.dependency_wait_us != 0)
                {
                    timing_.dependency_wait_us = timing.dependency_wait_us;
                }
                if (timing.registration_us != 0)
                {
                    timing_.registration_us = timing.registration_us;
                }
                if (timing.inclusive_elapsed_us != 0)
                {
                    timing_.inclusive_elapsed_us = timing.inclusive_elapsed_us;
                }
                if (IsActive())
                {
                    state_->UpdateTiming(operation_, timing_);
                }
            }

            void SetSizeCost(const AssetLoadSizeCost &size_cost) noexcept
            {
                if (size_cost.source_file_bytes)
                {
                    size_cost_.source_file_bytes = size_cost.source_file_bytes;
                }
                if (size_cost.decoded_payload_bytes)
                {
                    size_cost_.decoded_payload_bytes = size_cost.decoded_payload_bytes;
                }
                if (IsActive())
                {
                    state_->UpdateSizeCost(operation_, size_cost_);
                }
            }

            void SetKnownChildren(uint32_t known_children) noexcept
            {
                if (IsActive())
                {
                    state_->SetKnownChildren(operation_, known_children);
                }
            }

            void ChildCompleted() noexcept
            {
                if (IsActive())
                {
                    state_->ChildCompleted(operation_);
                }
            }

            void Succeed(AssetLoadDisposition disposition, const AssetID &result) noexcept
            {
                Finish(AssetLoadState::Succeeded, disposition, result, {});
            }

            void Fail(const std::string &diagnostic) noexcept
            {
                Finish(AssetLoadState::Failed, std::nullopt, std::nullopt, diagnostic);
            }

            AssetLoadOperationID ID() const noexcept { return operation_; }

        private:
            void Finish(
                AssetLoadState state,
                std::optional<AssetLoadDisposition> disposition,
                std::optional<AssetID> result,
                const std::string &diagnostic) noexcept
            {
                if (!IsActive())
                {
                    return;
                }
                state_->Complete(operation_, state, disposition, result, timing_, size_cost_, diagnostic);
                finished_ = true;
            }

            std::shared_ptr<detail::AssetLoadSessionState> state_;
            AssetLoadOperationID operation_ = 0;
            AssetLoadTiming timing_{};
            AssetLoadSizeCost size_cost_{};
            bool finished_ = false;
        };

        std::string MakeDiagnostic(
            AssetLoadOperationID operation,
            const std::string &display_path,
            AssetLoadPhase phase,
            const std::string &reason)
        {
            try
            {
                return "operation " + std::to_string(operation) + " [" + display_path + "] " +
                       PhaseName(phase) + ": " + reason;
            }
            catch (...)
            {
                return "asset load observation failure";
            }
        }

        uint16_t FloatToHalf(float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
            const uint32_t exponent = (bits >> 23) & 0xffu;
            uint32_t mantissa = bits & 0x7fffffu;
            if (exponent == 0xffu)
            {
                return static_cast<uint16_t>(sign | 0x7c00u | (mantissa != 0 ? 0x0200u : 0u));
            }

            int32_t half_exponent = static_cast<int32_t>(exponent) - 127 + 15;
            if (half_exponent >= 31)
            {
                return static_cast<uint16_t>(sign | 0x7c00u);
            }
            if (half_exponent <= 0)
            {
                if (half_exponent < -10)
                {
                    return sign;
                }
                mantissa = (mantissa | 0x800000u) >> (1 - half_exponent);
                return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
            }
            mantissa += 0x1000u;
            if ((mantissa & 0x800000u) != 0)
            {
                mantissa = 0;
                ++half_exponent;
                if (half_exponent >= 31)
                {
                    return static_cast<uint16_t>(sign | 0x7c00u);
                }
            }
            return static_cast<uint16_t>(sign |
                                         (static_cast<uint16_t>(half_exponent) << 10) |
                                         (mantissa >> 13));
        }

        data::TextureData ConvertHdrTexture(const image_io::ImageBuffer &image)
        {
            data::TextureData output{};
            const uint32_t divisor = std::max(1u,
                (image.width + kMaxImportedHdrWidth - 1) / kMaxImportedHdrWidth);
            output.width = std::max(1u, image.width / divisor);
            output.height = std::max(1u, image.height / divisor);
            output.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
            output.pixels.resize(static_cast<size_t>(output.width) * output.height * 8);

            for (uint32_t y = 0; y < output.height; ++y)
            {
                const uint32_t source_y = std::min(y * divisor, image.height - 1);
                for (uint32_t x = 0; x < output.width; ++x)
                {
                    const uint32_t source_x = std::min(x * divisor, image.width - 1);
                    const size_t source_offset =
                        (static_cast<size_t>(source_y) * image.width + source_x) * 4 * sizeof(float);
                    const size_t output_offset =
                        (static_cast<size_t>(y) * output.width + x) * 4 * sizeof(uint16_t);
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        float value = 0.0f;
                        std::memcpy(&value,
                                    image.pixels.data() + source_offset + channel * sizeof(float),
                                    sizeof(value));
                        const uint16_t half = FloatToHalf(value);
                        std::memcpy(output.pixels.data() + output_offset + channel * sizeof(uint16_t),
                                    &half, sizeof(half));
                    }
                }
            }
            return output;
        }
    }

    AssetManager AssetManager::instance_;
    AssetManager::~AssetManager() = default;
    AssetManager::AssetManager() : model_loader_(std::make_unique<Assimp_ModelLoader>()),
                                   shader_program_loader_(std::make_unique<ShaderProgramLoader>()),
                                   audio_loader_(std::make_unique<MiniAudio_AudioLoader>()),
                                   material_loader_(std::make_unique<MaterialLoader>()),
                                   level_loader_(std::make_unique<LevelLoader>())
    {
    }

    std::string AssetManager::Key(const std::string &path)
    {
        return CanonicalAssetPathKey(path);
    }

    AssetID AssetManager::LoadSync(const std::string &path)
    {
        return LoadSyncInternal(path, nullptr, std::nullopt, std::nullopt);
    }

    AssetLoadSession AssetManager::BeginLoadObservation()
    {
        return AssetLoadSession(std::make_shared<detail::AssetLoadSessionState>(
            detail::AllocateAssetLoadSessionID()));
    }

    AssetID AssetManager::LoadSync(
        const std::string &path,
        const AssetLoadSession &session)
    {
        return LoadSyncInternal(path, session.state_, std::nullopt, std::nullopt);
    }

    AssetID AssetManager::LoadSyncInternal(
        const std::string &path,
        const std::shared_ptr<detail::AssetLoadSessionState> &session_state,
        std::optional<AssetLoadOperationID> parent_operation,
        std::optional<AssetLoadOperationID> reserved_operation)
    {
        std::string extension = GetFileExtension(path);
        const AssetType type = ExtractAssetType(extension);
        const bool can_record = session_state &&
                                (reserved_operation.has_value() ||
                                 parent_operation.has_value() ||
                                 !session_state->IsSealed());
        const std::shared_ptr<detail::AssetLoadSessionState> observation_state =
            can_record ? session_state : nullptr;
        const std::string display_path = observation_state ? DisplayPath(path) : std::string{};
        const AssetLoadOperationID operation_id = reserved_operation
                                                       ? *reserved_operation
                                                       : (observation_state
                                                              ? observation_state->BeginOperation(
                                                                    display_path, type,
                                                                    parent_operation)
                                                              : 0);
        ObservedOperation observation(observation_state, operation_id);

        if (extension.empty())
        {
            if (observation.IsActive())
            {
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::CacheLookup,
                                                "path has no recognized extension"));
            }
            return AssetID();
        }

        if (type == AssetType::Undefined)
        {
            KP_LOG("AssetManagerLog", LOG_LEVEL_WARNING, "Unrecognize asset extension: %s ", extension.c_str());
            if (observation.IsActive())
            {
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::CacheLookup,
                                                "unsupported asset extension"));
            }
            return AssetID();
        }

        // Already loaded? Re-checked after loading too, so two concurrent
        // requests for the same file don't both register.
        auto find_cached = [this](AssetType type, const std::string &path) -> AssetID
        {
            const AssetCache *cache = FindCache(type);
            if (!cache)
            {
                return AssetID();
            }
            auto it = cache->path_index.find(Key(path));
            if (it == cache->path_index.end())
            {
                return AssetID();
            }
            return GetAsset(it->second) ? it->second : AssetID();
        };

        const auto cache_lookup_started = observation.IsActive()
                                              ? observation_state->Now()
                                              : std::chrono::steady_clock::time_point{};
        AssetID cached_id;
        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex_);
            cached_id = find_cached(type, path);
        }
        if (cached_id.IsValid())
        {
            if (observation.IsActive())
            {
                AssetLoadTiming timing{};
                timing.cache_lookup_us = ElapsedMicroseconds(
                    cache_lookup_started, observation_state->Now());
                observation.SetTiming(timing);
                observation.Succeed(AssetLoadDisposition::CacheHit, cached_id);
            }
            return cached_id;
        }

        if (observation.IsActive())
        {
            AssetLoadTiming timing{};
            timing.cache_lookup_us = ElapsedMicroseconds(
                cache_lookup_started, observation_state->Now());
            observation.SetTiming(timing);
            AssetLoadSizeCost size_cost{};
            size_cost.source_file_bytes = ProbeSourceFileSize(path);
            observation.SetSizeCost(size_cost);
        }

        // Disk I/O + parse under the loader lock: the loaders are shared instances.
        AssetRegisterInfo register_info{};
        if (observation.IsActive())
        {
            observation.SetPhase(AssetLoadPhase::WaitingForLoader);
        }
        const auto queue_wait_started = observation.IsActive()
                                            ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
        std::chrono::steady_clock::time_point loader_acquired{};
        std::chrono::steady_clock::time_point source_load_started{};
        std::chrono::steady_clock::time_point source_load_finished{};
        bool loaded = false;

        // Publish the source phase before taking the loader lock. The loader
        // call must stay serialized, while observation publication must not
        // occur under that lock.
        if (observation.IsActive())
        {
            observation.SetPhase(AssetLoadPhase::LoadSource);
        }
        try
        {
            {
                std::lock_guard<std::mutex> lock(load_mutex_);
                loader_acquired = observation.IsActive()
                                      ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
                source_load_started = observation.IsActive()
                                          ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
                loaded = LoadByExtension(path, type, register_info);
            }
            source_load_finished = observation.IsActive()
                                       ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
        }
        catch (...)
        {
            if (observation.IsActive())
            {
                source_load_finished = std::chrono::steady_clock::now();
                AssetLoadTiming timing{};
                timing.loader_queue_wait_us = ElapsedMicroseconds(
                    queue_wait_started, loader_acquired);
                timing.source_load_us = ElapsedMicroseconds(
                    source_load_started, source_load_finished);
                observation.SetTiming(timing);
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::LoadSource,
                                                "loader threw an exception"));
            }
            throw;
        }

        if (observation.IsActive())
        {
            AssetLoadTiming timing{};
            timing.loader_queue_wait_us = ElapsedMicroseconds(
                queue_wait_started, loader_acquired);
            timing.source_load_us = ElapsedMicroseconds(
                source_load_started, source_load_finished);
            observation.SetTiming(timing);
        }

        if (!loaded)
        {
            if (observation.IsActive())
            {
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::LoadSource,
                                                "loader rejected the source"));
            }
            return AssetID();
        }

        if (!IsValidResource(register_info.resource))
        {
            if (observation.IsActive())
            {
                observation.SetPhase(AssetLoadPhase::LoadSource);
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::LoadSource,
                                                "loader returned no resource"));
            }
            return AssetID();
        }

        if (observation.IsActive())
        {
            AssetLoadSizeCost size_cost{};
            size_cost.decoded_payload_bytes =
                MeasureDecodedPayload(*this, register_info);
            observation.SetSizeCost(size_cost);
        }

        if (observation.IsActive())
        {
            observation.SetPhase(AssetLoadPhase::ResolveDependencies);
            observation.SetKnownChildren(
                static_cast<uint32_t>(register_info.dependency_requests.size()));
        }

        // Loaders only declare dependencies. Resolve them after releasing the
        // shared loader lock; recursive LoadSync while it is held would
        // self-deadlock.
        const size_t declared_dependency_offset = register_info.dependencies.size();
        const auto dependency_wait_started = observation.IsActive()
                                                 ? observation_state->Now()
                                                 : std::chrono::steady_clock::time_point{};
        if (!register_info.dependency_requests.empty())
        {
            std::vector<AssetID> resolved_dependencies = std::move(register_info.dependencies);
            resolved_dependencies.reserve(resolved_dependencies.size() +
                                           register_info.dependency_requests.size());
            for (const AssetRegisterInfo::DependencyRequest &request : register_info.dependency_requests)
            {
                AssetID dependency;
                try
                {
                    dependency = LoadSyncInternal(
                        request.path, observation_state,
                        observation.IsActive()
                            ? std::optional<AssetLoadOperationID>(observation.ID())
                            : std::nullopt,
                        std::nullopt);
                }
                catch (...)
                {
                    observation.ChildCompleted();
                    if (observation.IsActive())
                    {
                        AssetLoadTiming timing{};
                        timing.dependency_wait_us = ElapsedMicroseconds(
                            dependency_wait_started, observation_state->Now());
                        observation.SetTiming(timing);
                        observation.Fail(MakeDiagnostic(
                            observation.ID(), display_path,
                            AssetLoadPhase::ResolveDependencies,
                            "dependency load threw an exception"));
                    }
                    throw;
                }
                observation.ChildCompleted();
                if (!dependency.IsValid() || dependency.type != request.expected_type)
                {
                    KP_LOG("AssetManagerLog", LOG_LEVEL_ERROR,
                           "Failed to resolve dependency %s for %s",
                           request.path.c_str(), path.c_str());
                    if (observation.IsActive())
                    {
                        AssetLoadTiming timing{};
                        timing.dependency_wait_us = ElapsedMicroseconds(
                            dependency_wait_started, observation_state->Now());
                        observation.SetTiming(timing);
                        observation.Fail(MakeDiagnostic(
                            observation.ID(), display_path,
                            AssetLoadPhase::ResolveDependencies,
                            "dependency failed"));
                    }
                    return AssetID();
                }
                resolved_dependencies.push_back(dependency);
            }
            register_info.dependencies = std::move(resolved_dependencies);
        }

        if (observation.IsActive())
        {
            AssetLoadTiming timing{};
            timing.dependency_wait_us = ElapsedMicroseconds(
                dependency_wait_started, observation_state->Now());
            observation.SetTiming(timing);
        }

        AssetID result;
        AssetLoadDisposition disposition = AssetLoadDisposition::LoadedAndRegistered;
        std::string registration_failure;
        const auto registration_started = observation.IsActive()
                                              ? observation_state->Now()
                                              : std::chrono::steady_clock::time_point{};
        if (observation.IsActive())
        {
            observation.SetPhase(AssetLoadPhase::Register);
        }
        try
        {
            std::lock_guard<std::recursive_mutex> lock(state_mutex_);
            if (AssetID cached = find_cached(type, path); cached.IsValid())
            {
                result = cached;
                disposition = AssetLoadDisposition::LoadedThenDeduplicated;
            }
            else
            {
                // A resolved dependency can be unregistered while this load is
                // resolving children. Revalidate immediately before installing
                // the parent edges.
                for (size_t index = 0; index < register_info.dependency_requests.size(); ++index)
                {
                    const size_t dependency_index = declared_dependency_offset + index;
                    if (dependency_index >= register_info.dependencies.size() ||
                        register_info.dependencies[dependency_index].type !=
                            register_info.dependency_requests[index].expected_type ||
                        !GetAsset(register_info.dependencies[dependency_index]))
                    {
                        registration_failure = "dependency disappeared before registration";
                        break;
                    }
                }
                if (registration_failure.empty())
                {
                    result = RegisterAsset(register_info);
                    if (result.IsValid())
                    {
                        Cache(type).path_index[Key(GetAsset(result)->GetPath())] = result;
                    }
                    else
                    {
                        registration_failure = "asset registration failed";
                    }
                }
            }
            register_info.dependency_requests.clear();
        }
        catch (...)
        {
            if (observation.IsActive())
            {
                AssetLoadTiming timing{};
                timing.registration_us = ElapsedMicroseconds(
                    registration_started, observation_state->Now());
                observation.SetTiming(timing);
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::Register,
                                                "registration threw an exception"));
            }
            throw;
        }

        if (observation.IsActive())
        {
            AssetLoadTiming timing{};
            timing.registration_us = ElapsedMicroseconds(
                registration_started, observation_state->Now());
            observation.SetTiming(timing);
            if (!registration_failure.empty())
            {
                observation.Fail(MakeDiagnostic(observation.ID(), display_path,
                                                AssetLoadPhase::Register,
                                                registration_failure));
            }
            else
            {
                observation.Succeed(disposition, result);
            }
        }
        return result;
    }

    std::future<AssetID> AssetManager::LoadAsync(const std::string &path)
    {
        // Same pipeline as LoadSync, offloaded to a worker thread. Loads serialize
        // on load_mutex_, so concurrent calls never race the shared loaders.
        // Note: destroying this future without get()/wait() blocks until the load
        // finishes (std::async semantics).
        return std::async(std::launch::async, [this, path]()
                          { return LoadSync(path); });
    }

    std::future<AssetID> AssetManager::LoadAsync(
        const std::string &path,
        AssetLoadSession session)
    {
        const std::shared_ptr<detail::AssetLoadSessionState> state = session.state_;
        const AssetType type = ExtractAssetType(GetFileExtension(path));
        const std::string display_path = state ? DisplayPath(path) : std::string{};
        const AssetLoadOperationID reserved = state
                                                  ? state->BeginOperation(
                                                        display_path, type,
                                                        std::nullopt)
                                                  : 0;
        try
        {
            return std::async(
                std::launch::async,
                [this, path, state, reserved]()
                {
                    return LoadSyncInternal(
                        path, state, std::nullopt,
                        reserved == 0
                            ? std::nullopt
                            : std::optional<AssetLoadOperationID>(reserved));
                });
        }
        catch (...)
        {
            if (reserved != 0)
            {
                ObservedOperation observation(state, reserved);
                observation.Fail(MakeDiagnostic(
                    reserved, display_path, AssetLoadPhase::WaitingForLoader,
                    "async dispatch failed"));
            }
            throw;
        }
    }

    const AssetCache *AssetManager::FindCache(AssetType type) const
    {
        auto it = caches_.find(type);
        if (it == caches_.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    AssetCache *AssetManager::FindCache(AssetType type)
    {
        auto it = caches_.find(type);
        if (it == caches_.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    AssetCache &AssetManager::Cache(AssetType type)
    {
        return caches_[type];
    }

    AssetID AssetManager::RegisterAsset(AssetRegisterInfo &info)
    {
        if (!IsValidResource(info.resource))
        {
            return AssetID();
        }

        std::lock_guard<std::recursive_mutex> lock(state_mutex_);

        AssetType type = info.type;
        AssetCache &cache = Cache(type);
        AssetHandle handle = cache.handles.Create();

        if (handle.id == cache.assets.size())
        {
            cache.assets.emplace_back();
        }

        std::unique_ptr<Asset> asset = std::make_unique<Asset>();
        asset->resource = std::move(info.resource);
        asset->id.type = type;
        asset->id.id = handle.id;
        asset->id.generation = handle.generation;
        asset->abs_path = std::move(info.path);
        asset->name = std::move(info.name);
        asset->ref_assets = std::move(info.ref_assets);
        asset->dependencies = std::move(info.dependencies);

        AssetID id(handle.id, handle.generation, type);
        cache.assets[handle.id] = std::move(asset);
        AddReferences(id, cache.assets[handle.id]->dependencies);

        std::string type_name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "Register Aseset [%s|%s|%llu] from %s successfully",
               type_name.c_str(),
               cache.assets[handle.id]->GetName().c_str(),
               id.Pack(),
               cache.assets[handle.id]->GetPath().c_str());
        return id;
    }

    Asset *AssetManager::GetAsset(const AssetID &id)
    {
        if (!id.IsValid())
        {
            return nullptr;
        }
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        const AssetCache *cache = FindCache(id.type);
        if (!cache || id.id >= cache->assets.size())
        {
            return nullptr;
        }
        // Stale id (recycled slot) must resolve to null, never to the new occupant.
        if (!cache->handles.IsHandleValid(AssetHandle(id.id, id.generation)))
        {
            return nullptr;
        }
        return cache->assets[id.id].get();
    }

    AssetID AssetManager::ResolveDependency(const AssetID &owner, size_t dependency_index,
                                             AssetType expected_type)
    {
        if (!owner.IsValid() || expected_type == AssetType::Undefined)
        {
            return AssetID();
        }

        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        const AssetCache *cache = FindCache(owner.type);
        if (!cache || owner.id >= cache->assets.size() ||
            !cache->handles.IsHandleValid(AssetHandle(owner.id, owner.generation)) ||
            !cache->assets[owner.id])
        {
            return AssetID();
        }

        const std::vector<AssetID> &dependencies = cache->assets[owner.id]->dependencies;
        if (dependency_index >= dependencies.size())
        {
            return AssetID();
        }

        const AssetID dependency = dependencies[dependency_index];
        if (dependency.type != expected_type || !GetAsset(dependency))
        {
            return AssetID();
        }
        return dependency;
    }

    void AssetManager::UnRegisterAsset(const AssetID &id)
    {
        if (!id.IsValid())
        {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        AssetCache *cache = FindCache(id.type);
        if (!cache || id.id >= cache->assets.size())
        {
            return;
        }
        // Stale id must not unregister the new occupant of a recycled slot.
        if (!cache->handles.IsHandleValid(AssetHandle(id.id, id.generation)))
        {
            return;
        }

        Asset *asset = cache->assets[id.id].get();
        if (!asset || !CanDelete(asset))
        {
            return;
        }

        RemoveReferences(asset->GetID(), asset->GetDependencies());

        cache->path_index.erase(Key(asset->GetPath()));

        KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
               "Unregister asset[%s, %llu] successfully",
               asset->GetName().c_str(), id.Pack());

        cache->assets[id.id].reset();
        cache->handles.Destroy(AssetHandle(id.id, id.generation));
    }

    bool AssetManager::CanDelete(const Asset *asset)
    {
        if (!asset)
            return true;

        if (!asset->GetRefs().empty())
        {
            KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
                   "Asset [%s, %llu] is still referenced by %zu assets",
                   asset->GetName().c_str(),
                   asset->GetID().Pack(),
                   asset->GetRefs().size());

            // Print who references it (very useful for debugging)
            for (const auto &ref_id : asset->GetRefs())
            {
                KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG,
                       "  Referenced by AssetID: %llu", ref_id.Pack());
            }

            return false;
        }

        return true;
    }

    void AssetManager::AddReferences(const AssetID &from, const std::vector<AssetID> &to_list)
    {
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);

        for (const auto &to : to_list)
        {
            auto asset = GetAsset(to);
            if (!asset)
                continue;

            if (std::find(asset->ref_assets.begin(), asset->ref_assets.end(), from) == asset->ref_assets.end())
            {
                asset->ref_assets.push_back(from);
#ifdef DEBUG
                KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "asset[%s] ref %llu", asset->GetName().c_str(), from.Pack());
#endif
            }
        }
    }
    void AssetManager::RemoveReferences(const AssetID &from, const std::vector<AssetID> &to_list)
    {
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        for (const auto &to_id : to_list)
        {
            Asset *target_asset = GetAsset(to_id);
            if (!target_asset)
            {
                continue;
            }

            auto &refs = target_asset->ref_assets;

            refs.erase(
                std::remove_if(refs.begin(), refs.end(), [&from](const AssetID &id)
                               { return id == from; }),
                refs.end());
#ifdef DEBUG
            KP_LOG("AssetManagerLog", LOG_LEVEL_DEBUG, "asset[%s, %llu] unref %llu", target_asset->GetName().c_str(), target_asset->GetID().Pack(), from.Pack());
#endif
        }
    }

    bool AssetManager::LoadByExtension(const std::string &path, AssetType type, AssetRegisterInfo &info)
    {
        if (type == AssetType::KPAT_Model)
        {
            assert(model_loader_);
            return model_loader_->Load(path, ModelGeometryType::KPMG_Mesh, info);
        }
        else if (type == AssetType::KPAT_Texture)
        {
            image_io::ImageDecodeResult decoded = image_io::DecodeImageFile(path);
            if (!decoded.result.success)
            {
                KP_LOG("AssetManagerLog", LOG_LEVEL_ERROR, "Failed to load image from %s: %s",
                       path.c_str(), decoded.result.diagnostic.c_str());
                return false;
            }

            std::shared_ptr<TextureResource> texture = std::make_shared<TextureResource>();
            texture->channel_count = 4;
            if (decoded.image.format == image_io::ImagePixelFormat::Rgba32Float)
            {
                *texture->data = ConvertHdrTexture(decoded.image);
            }
            else
            {
                texture->data->width = decoded.image.width;
                texture->data->height = decoded.image.height;
                texture->data->format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
                texture->data->pixels = std::move(decoded.image.pixels);
            }

            info.type = AssetType::KPAT_Texture;
            info.path = path;
            info.name = std::string(magic_enum::enum_name(info.type)) + ExtractNameFromPath(path);
            info.resource = std::move(texture);
            return true;
        }
        else if (type == AssetType::KPAT_ShaderProgram)
        {
            assert(shader_program_loader_);
            return shader_program_loader_->Load(path, info);
        }
        else if (type == AssetType::KPAT_Audio)
        {
            assert(audio_loader_);
            return audio_loader_->LoadFromFile(path, info);
        }
        else if (type == AssetType::KPAT_Material)
        {
            assert(material_loader_);
            return material_loader_->Load(path, info);
        }
        else if (type == AssetType::KPAT_Level)
        {
            assert(level_loader_);
            return level_loader_->Load(path, info);
        }

        std::string name = std::string(magic_enum::enum_name(type));
        KP_LOG("AssetManagerLog", LOG_LEVEL_WARNING, "Failed to found suitable loader for Assettype: %s", name.c_str());
        return false;
    }

}
