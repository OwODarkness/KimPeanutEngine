#include "terrain_generation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>

#include "math/noise2d.h"
#include "terrain_hydraulic_erosion.h"

namespace kpengine::terrain
{
    void EvaluationExecutionControl::Pause()
    {
        std::lock_guard lock(mutex_);
        paused_ = true;
        step_tokens_ = 0;
    }

    void EvaluationExecutionControl::Resume()
    {
        {
            std::lock_guard lock(mutex_);
            paused_ = false;
            step_tokens_ = 0;
        }
        wake_.notify_all();
    }

    void EvaluationExecutionControl::Step()
    {
        {
            std::lock_guard lock(mutex_);
            paused_ = true;
            ++step_tokens_;
        }
        wake_.notify_all();
    }

    bool EvaluationExecutionControl::IsPaused() const
    {
        std::lock_guard lock(mutex_);
        return paused_;
    }

    bool EvaluationExecutionControl::WaitForNode(const std::atomic_bool *cancelled)
    {
        std::unique_lock lock(mutex_);
        while (paused_ && step_tokens_ == 0 && !(cancelled && cancelled->load()))
        {
            wake_.wait_for(lock, std::chrono::milliseconds(20));
        }
        if (cancelled && cancelled->load()) return false;
        if (paused_ && step_tokens_ > 0) --step_tokens_;
        return true;
    }

    namespace
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        double SampleTerrainPerlin(const double x, const double z, const std::uint64_t seed)
        {
            constexpr double cosine = 0.7986355100472928;
            constexpr double sine = 0.6018150231520483;
            const double rotated_x = cosine * x - sine * z;
            const double rotated_z = sine * x + cosine * z;
            return 0.5 * (math::PerlinNoise2D(x, z, seed) +
                          math::PerlinNoise2D(rotated_x, rotated_z, seed));
        }

        struct RemapCurve final
        {
            std::vector<std::array<double, 2>> points;
            std::vector<double> tangents;

            explicit RemapCurve(std::vector<std::array<double, 2>> input)
                : points(std::move(input)), tangents(points.size())
            {
                if (points.size() < 2 || points.size() > 32 ||
                    std::abs(points.front()[0]) > 1.0e-9 ||
                    std::abs(points.back()[0] - 1.0) > 1.0e-9)
                    throw std::invalid_argument("remap curve needs 2..32 points spanning x=0 to x=1");
                std::vector<double> widths(points.size() - 1);
                std::vector<double> secants(points.size() - 1);
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const auto [x, y] = points[i];
                    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x > 1.0 ||
                        y < 0.0 || y > 1.0 || (i > 0 &&
                        (x <= points[i - 1][0] || y < points[i - 1][1])))
                        throw std::invalid_argument("remap curve points must be finite, ordered and monotone in [0, 1]");
                    if (i > 0)
                    {
                        widths[i - 1] = x - points[i - 1][0];
                        secants[i - 1] = (y - points[i - 1][1]) / widths[i - 1];
                    }
                }
                if (points.size() == 2)
                {
                    tangents[0] = tangents[1] = secants[0];
                    return;
                }
                for (std::size_t i = 1; i + 1 < points.size(); ++i)
                {
                    const double before = secants[i - 1];
                    const double after = secants[i];
                    if (before == 0.0 || after == 0.0)
                    {
                        tangents[i] = 0.0;
                        continue;
                    }
                    const double w1 = 2.0 * widths[i] + widths[i - 1];
                    const double w2 = widths[i] + 2.0 * widths[i - 1];
                    tangents[i] = (w1 + w2) / (w1 / before + w2 / after);
                }
                const auto endpoint_tangent = [](double h0, double h1, double d0, double d1)
                {
                    double tangent = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
                    if (tangent * d0 <= 0.0) return 0.0;
                    if (d0 * d1 < 0.0 && std::abs(tangent) > 3.0 * std::abs(d0))
                        return 3.0 * d0;
                    return tangent;
                };
                tangents.front() = endpoint_tangent(widths[0], widths[1], secants[0], secants[1]);
                tangents.back() = endpoint_tangent(widths.back(), widths[widths.size() - 2],
                    secants.back(), secants[secants.size() - 2]);
            }

            double Evaluate(double x) const
            {
                if (x <= points.front()[0]) return points.front()[1];
                if (x >= points.back()[0]) return points.back()[1];
                const auto upper = std::upper_bound(points.begin(), points.end(), x,
                    [](double value, const auto &point) { return value < point[0]; });
                const std::size_t i = static_cast<std::size_t>(upper - points.begin() - 1);
                const double h = points[i + 1][0] - points[i][0];
                const double t = (x - points[i][0]) / h;
                const double t2 = t * t;
                const double t3 = t2 * t;
                return (2.0 * t3 - 3.0 * t2 + 1.0) * points[i][1] +
                    (t3 - 2.0 * t2 + t) * h * tangents[i] +
                    (-2.0 * t3 + 3.0 * t2) * points[i + 1][1] +
                    (t3 - t2) * h * tangents[i + 1];
            }
        };

        void HashBytes(std::uint64_t &hash, const void *data, std::size_t size)
        {
            const auto *bytes = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                hash = (hash ^ bytes[i]) * kFnvPrime;
            }
        }

        void HashString(std::uint64_t &hash, std::string_view value)
        {
            HashBytes(hash, value.data(), value.size());
        }

        std::uint64_t HashTerrainValue(const TerrainValue2D &field)
        {
            std::uint64_t hash = kFnvOffset;
            const auto &domain = field.Domain();
            HashBytes(hash, &domain.width, sizeof(domain.width));
            HashBytes(hash, &domain.height, sizeof(domain.height));
            HashBytes(hash, &domain.origin_x_m, sizeof(double));
            HashBytes(hash, &domain.origin_z_m, sizeof(double));
            HashBytes(hash, &domain.spacing_x_m, sizeof(double));
            HashBytes(hash, &domain.spacing_z_m, sizeof(double));
            HashBytes(hash, &domain.datum_y_m, sizeof(double));
            const TerrainValueKind kind = field.Kind();
            HashBytes(hash, &kind, sizeof(kind));
            for (float sample : field.Samples())
            {
                const auto bits = std::bit_cast<std::uint32_t>(sample);
                HashBytes(hash, &bits, sizeof(bits));
            }
            if (const auto *layered = field.AsLayeredHeightfield())
            {
                const std::array<const std::vector<float> *, 5> channels{{
                    &layered->BedrockElevationMeters(), &layered->SoilThicknessMeters(),
                    &layered->SandThicknessMeters(), &layered->WaterDepthMeters(),
                    &layered->SuspendedSedimentKgPerSquareMeter()}};
                for (const auto *channel : channels)
                    for (const float sample : *channel)
                    {
                        const auto bits = std::bit_cast<std::uint32_t>(sample);
                        HashBytes(hash, &bits, sizeof(bits));
                    }
            }
            return hash;
        }

        std::uint64_t NodeKey(const TerrainRecipe &recipe, const RecipeNode &node,
                              const OperatorInputs &inputs)
        {
            std::uint64_t hash = kFnvOffset;
            HashBytes(hash, &recipe.seed, sizeof(recipe.seed));
            HashString(hash, node.id);
            HashString(hash, node.operator_id);
            HashBytes(hash, &node.operator_version, sizeof(node.operator_version));
            HashString(hash, node.parameters.dump());
            const auto &domain = recipe.domain;
            HashBytes(hash, &domain.width, sizeof(domain.width));
            HashBytes(hash, &domain.height, sizeof(domain.height));
            HashBytes(hash, &domain.origin_x_m, sizeof(double));
            HashBytes(hash, &domain.origin_z_m, sizeof(double));
            HashBytes(hash, &domain.spacing_x_m, sizeof(double));
            HashBytes(hash, &domain.spacing_z_m, sizeof(double));
            HashBytes(hash, &domain.datum_y_m, sizeof(double));
            for (const auto &[name, field] : inputs)
            {
                HashString(hash, name);
                const auto input_hash = HashTerrainValue(*field);
                HashBytes(hash, &input_hash, sizeof(input_hash));
            }
            return hash;
        }

        std::uint64_t NodeSeed(const TerrainRecipe &recipe, const RecipeNode &node)
        {
            std::uint64_t hash = kFnvOffset;
            HashBytes(hash, &recipe.seed, sizeof(recipe.seed));
            HashString(hash, node.id);
            HashBytes(hash, &node.operator_version, sizeof(node.operator_version));
            HashString(hash, node.operator_id);
            return hash;
        }

        const ScalarField2D &RequireScalarField(const TerrainValue &value,
                                                const char *input_name)
        {
            const ScalarField2D *const field = value ? value->AsScalarField() : nullptr;
            if (field == nullptr)
                throw std::invalid_argument(std::string("terrain input is not a scalar field: ") + input_name);
            return *field;
        }

        std::uint32_t MurmurFinalize32(std::uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x85ebca6bu;
            value ^= value >> 13;
            value *= 0xc2b2ae35u;
            value ^= value >> 16;
            return value;
        }

        std::uint64_t NextTerrainRandom(std::uint64_t &state)
        {
            state += 0x9e3779b97f4a7c15ull;
            std::uint64_t value = state;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }
    }

    nlohmann::json TerrainRecipe::ToJson() const
    {
        nlohmann::json serialized_nodes = nlohmann::json::array();
        auto ordered = nodes;
        std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            return a.id < b.id;
        });
        for (const auto &node : ordered)
        {
            nlohmann::json inputs_json = nlohmann::json::object();
            for (const auto &[port, source] : node.inputs)
            {
                inputs_json[port] = {{"node", source.node}, {"port", source.port}};
            }
            serialized_nodes.push_back({{"id", node.id}, {"operator", node.operator_id},
                {"version", node.operator_version}, {"parameters", node.parameters},
                {"inputs", std::move(inputs_json)}});
        }
        return {{"schema_version", schema_version}, {"seed", seed},
            {"domain", {{"width", domain.width}, {"height", domain.height},
                {"origin_x_m", domain.origin_x_m}, {"origin_z_m", domain.origin_z_m},
                {"spacing_x_m", domain.spacing_x_m}, {"spacing_z_m", domain.spacing_z_m},
                {"datum_y_m", domain.datum_y_m}}}, {"nodes", std::move(serialized_nodes)}};
    }

    TerrainRecipe TerrainRecipe::FromJson(const nlohmann::json &json)
    {
        if (!json.is_object() || json.at("schema_version").get<std::uint32_t>() != 1)
        {
            throw std::invalid_argument("unsupported terrain recipe schema version");
        }
        TerrainRecipe recipe;
        recipe.schema_version = 1;
        recipe.seed = json.at("seed").get<std::uint64_t>();
        const auto &domain_json = json.at("domain");
        recipe.domain = {domain_json.at("width").get<std::uint32_t>(),
            domain_json.at("height").get<std::uint32_t>(),
            domain_json.at("origin_x_m").get<double>(),
            domain_json.at("origin_z_m").get<double>(),
            domain_json.at("spacing_x_m").get<double>(),
            domain_json.at("spacing_z_m").get<double>(),
            domain_json.at("datum_y_m").get<double>()};
        recipe.domain.SampleCount(4u * 1024u * 1024u);
        for (const auto &node_json : json.at("nodes"))
        {
            RecipeNode node;
            node.id = node_json.at("id").get<std::string>();
            node.operator_id = node_json.at("operator").get<std::string>();
            node.operator_version = node_json.at("version").get<std::uint32_t>();
            node.parameters = node_json.at("parameters");
            if (!node.parameters.is_object())
            {
                throw std::invalid_argument("operator parameters must be a JSON object");
            }
            for (auto it = node_json.at("inputs").begin(); it != node_json.at("inputs").end(); ++it)
            {
                node.inputs.emplace(it.key(), OutputRef{
                    it.value().at("node").get<std::string>(),
                    it.value().at("port").get<std::string>()});
            }
            recipe.nodes.push_back(std::move(node));
        }
        return recipe;
    }

    bool OperatorRegistry::Register(OperatorDescriptor descriptor, std::string &diagnostic)
    {
        if (descriptor.id.empty() || descriptor.version == 0 || !descriptor.evaluate ||
            descriptor.outputs.empty() || descriptor.outputs.size() > 64 ||
            descriptor.inputs.size() > 64)
        {
            diagnostic = "operator requires an ID, positive version, callback and output port";
            return false;
        }
        const auto key = std::make_pair(descriptor.id, descriptor.version);
        if (!descriptors_.emplace(key, std::move(descriptor)).second)
        {
            diagnostic = "operator ID and version are already registered";
            return false;
        }
        diagnostic.clear();
        return true;
    }

    const OperatorDescriptor *OperatorRegistry::Find(std::string_view id, std::uint32_t version) const
    {
        const auto found = descriptors_.find({std::string(id), version});
        return found == descriptors_.end() ? nullptr : &found->second;
    }

    bool OperatorRegistry::RegisterBuiltins(std::string &diagnostic)
    {
        const auto field_from_samples = [](const OperatorContext &context, std::vector<float> samples) {
            std::string error;
            auto field = ScalarField2D::Create(context.domain, std::move(samples),
                                               context.maximum_samples, error);
            if (!field) throw std::runtime_error(error);
            return field;
        };
        if (!Register({"terrain.scalar.constant", 1, {}, {{"value", PortType::ScalarField2D}},
            [](const OperatorContext &context, const nlohmann::json &parameters,
               const OperatorInputs &) {
                if (context.cancelled && context.cancelled->load())
                    throw std::runtime_error("evaluation cancelled");
                const float value = parameters.at("value").get<float>();
                if (!std::isfinite(value)) throw std::invalid_argument("constant value must be finite");
                const auto count = context.domain.SampleCount(context.maximum_samples);
                std::string diagnostic;
                auto field = ScalarField2D::Create(context.domain,
                    std::vector<float>(count, value), context.maximum_samples, diagnostic);
                if (!field) throw std::runtime_error(diagnostic);
                return OperatorOutputs{{"value", std::move(field)}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.constant", 1, {}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &parameters,
                                 const OperatorInputs &) {
                const float value = parameters.at("height_m").get<float>();
                if (!std::isfinite(value)) throw std::invalid_argument("height must be finite meters");
                return OperatorOutputs{{"height", field_from_samples(context,
                    std::vector<float>(context.domain.SampleCount(context.maximum_samples), value))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.raster", 1, {}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &) {
                const auto width = p.at("width").get<std::uint32_t>();
                const auto height = p.at("height").get<std::uint32_t>();
                const double ox = p.at("origin_x_m").get<double>();
                const double oz = p.at("origin_z_m").get<double>();
                const double sx = p.at("spacing_x_m").get<double>();
                const double sz = p.at("spacing_z_m").get<double>();
                GridDomain2D source_domain{width, height, ox, oz, sx, sz, 0.0};
                const auto source_count = source_domain.SampleCount(context.maximum_samples);
                const auto source = p.at("samples_m").get<std::vector<float>>();
                if (source.size() != source_count)
                    throw std::invalid_argument("height raster samples do not match dimensions");
                for (float value : source)
                    if (!std::isfinite(value)) throw std::invalid_argument("height raster must be finite meters");
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        const double gx = std::clamp((wx - ox) / sx, 0.0, static_cast<double>(width - 1));
                        const double gy = std::clamp((wz - oz) / sz, 0.0, static_cast<double>(height - 1));
                        const auto x0 = static_cast<std::uint32_t>(std::floor(gx));
                        const auto y0 = static_cast<std::uint32_t>(std::floor(gy));
                        const auto x1 = std::min(x0 + 1, width - 1);
                        const auto y1 = std::min(y0 + 1, height - 1);
                        const double tx = gx - x0, ty = gy - y0;
                        const auto at = [&source, width](std::uint32_t ix, std::uint32_t iy) {
                            return static_cast<double>(source[static_cast<std::size_t>(iy) * width + ix]);
                        };
                        samples[static_cast<std::size_t>(y) * context.domain.width + x] =
                            static_cast<float>((1.0 - ty) * ((1.0 - tx) * at(x0, y0) + tx * at(x1, y0)) +
                                               ty * ((1.0 - tx) * at(x0, y1) + tx * at(x1, y1)));
                    }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.perlin_fbm", 1, {}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &) {
                const std::uint32_t lattice_size = p.value("lattice_size", 4u);
                const std::uint32_t octaves = p.value("octaves", 4u);
                const double persistence = p.value("persistence", 0.5);
                const double lacunarity = p.value("lacunarity", 2.0);
                if (context.domain.width != context.domain.height || context.domain.width == 0 ||
                    lattice_size == 0 || lattice_size > 64 || octaves == 0 || octaves > 16 ||
                    !std::isfinite(persistence) || persistence < 0.0 || persistence > 1.0 ||
                    !std::isfinite(lacunarity) || lacunarity < 1.0 || lacunarity > 8.0)
                    throw std::invalid_argument(
                        "Perlin fBm needs a square domain, lattice size in [1, 64], 1..16 octaves, "
                        "persistence in [0, 1] and lacunarity in [1, 8]");

                const std::uint32_t seed_hash = MurmurFinalize32(
                    static_cast<std::uint32_t>(context.seed));
                const double random_degrees = static_cast<double>(seed_hash % 100u);
                const double offset = lattice_size * std::sin(
                    random_degrees * 3.14159265358979323846 / 180.0);
                const double map_size = context.domain.width;
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                {
                    if ((y & 15u) == 0u && context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        double frequency = 1.0;
                        double amplitude = 1.0;
                        double value = 0.0;
                        for (std::uint32_t octave = 0; octave < octaves; ++octave)
                        {
                            const double scaled_x = x * lattice_size * frequency / map_size + offset;
                            const double scaled_y = y * lattice_size * frequency / map_size + offset;
                            value += amplitude * SampleTerrainPerlin(scaled_x, scaled_y, 0);
                            frequency *= lacunarity;
                            amplitude *= persistence;
                        }
                        samples[static_cast<std::size_t>(y) * context.domain.width + x] =
                            static_cast<float>(0.5 + 0.5 * value);
                    }
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.macro_landform", 1,
            {{"detail", PortType::Heightfield}},
            {{"height", PortType::Heightfield},
             {"edge_mask", PortType::ScalarField2D},
             {"mountain_mask", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double center_x = p.value("center_x_m",
                    context.domain.origin_x_m + (context.domain.width - 1) * context.domain.spacing_x_m * 0.5);
                const double center_z = p.value("center_z_m",
                    context.domain.origin_z_m + (context.domain.height - 1) * context.domain.spacing_z_m * 0.5);
                const double coast_inner_radius = p.value("coast_inner_radius", 0.65);
                const double coast_outer_radius = p.value("coast_outer_radius", 1.10);
                const double coast_warp_amplitude = p.value("coast_warp_amplitude", 0.10);
                const double coast_warp_frequency = p.value("coast_warp_frequency_per_m", 0.012);
                const double coast_depth = p.value("coast_depth_m", 130.0);
                const double base_height = p.value("base_height_m", 24.0);
                const double landform_amplitude = p.value("landform_amplitude_m", 48.0);
                const double base_terrain_weight = p.value("base_terrain_weight", 0.55);
                const double macro_weight = p.value("macro_weight", 0.20);
                const double mountain_weight = p.value("mountain_weight", 0.25);
                const double macro_frequency = p.value("macro_frequency_per_m", 0.004);
                const double mountain_region_frequency = p.value("mountain_region_frequency_per_m", 0.0028);
                const double mountain_frequency = p.value("mountain_frequency_per_m", 0.006);
                const std::uint32_t mountain_octaves = p.value("mountain_octaves", 4u);
                const double nyquist = 0.5 / std::max(context.domain.spacing_x_m,
                                                     context.domain.spacing_z_m);
                const std::array<double, 14> numeric_parameters{
                    center_x, center_z, coast_inner_radius, coast_outer_radius,
                    coast_warp_amplitude, coast_warp_frequency, coast_depth,
                    base_height, landform_amplitude, base_terrain_weight, macro_weight,
                    mountain_weight, macro_frequency, mountain_region_frequency};
                if (std::any_of(numeric_parameters.begin(), numeric_parameters.end(),
                                [](double value) { return !std::isfinite(value); }) ||
                    coast_inner_radius < 0.0 || coast_outer_radius <= coast_inner_radius ||
                    coast_warp_amplitude < 0.0 || coast_warp_amplitude > 0.5 || coast_warp_frequency <= 0.0 ||
                    coast_warp_frequency > nyquist || coast_depth < 0.0 || landform_amplitude < 0.0 ||
                    base_terrain_weight < 0.0 || macro_weight < 0.0 || mountain_weight < 0.0 ||
                    std::abs(base_terrain_weight + macro_weight + mountain_weight - 1.0) > 1.0e-6 ||
                    macro_frequency <= 0.0 || macro_frequency > nyquist ||
                    mountain_region_frequency <= 0.0 || mountain_region_frequency > nyquist ||
                    mountain_frequency <= 0.0 || mountain_frequency > nyquist ||
                    mountain_octaves == 0 || mountain_octaves > 8)
                    throw std::invalid_argument("macro landform parameters are outside their finite, sampled ranges");

                const auto smoothstep = [](const double lower, const double upper, const double value) {
                    const double t = std::clamp((value - lower) / (upper - lower), 0.0, 1.0);
                    return t * t * (3.0 - 2.0 * t);
                };
                const auto fbm = [&](const double x, const double z, double frequency,
                                     const std::uint32_t octaves, const double persistence,
                                     const std::uint64_t seed) {
                    double sum = 0.0;
                    double weight = 1.0;
                    double total_weight = 0.0;
                    for (std::uint32_t octave = 0; octave < octaves && frequency <= nyquist;
                         ++octave, frequency *= 2.0, weight *= persistence)
                    {
                        sum += SampleTerrainPerlin(x * frequency, z * frequency,
                                                   seed + octave) * weight;
                        total_weight += weight;
                    }
                    return total_weight > 0.0 ? sum / total_weight : 0.0;
                };
                const double half_x = (context.domain.width - 1) * context.domain.spacing_x_m * 0.5;
                const double half_z = (context.domain.height - 1) * context.domain.spacing_z_m * 0.5;
                const std::size_t sample_count = context.domain.SampleCount(context.maximum_samples);
                std::vector<float> heights(sample_count);
                std::vector<float> edge_masks(sample_count);
                std::vector<float> mountains(sample_count);
                const auto &detail = inputs.at("detail")->Samples();
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                {
                    if ((y & 15u) == 0u && context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * context.domain.width + x;
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        const double dx = wx - center_x;
                        const double dz = wz - center_z;
                        const double radius = std::hypot(dx / half_x, dz / half_z) +
                            fbm(wx, wz, coast_warp_frequency, 3, 0.5,
                                context.node_seed + 11) * coast_warp_amplitude;
                        const double edge_mask = smoothstep(
                            coast_inner_radius, coast_outer_radius, radius);
                        const double mountain_region_noise = 0.5 + 0.5 * fbm(
                            wx, wz, mountain_region_frequency, 3, 0.55,
                            context.node_seed + 61);
                        const double mountain_mask = smoothstep(0.60, 0.80,
                                                               mountain_region_noise);
                        const double macro_noise = fbm(wx, wz, macro_frequency, 3, 0.5,
                                                        context.node_seed + 47);
                        double ridged_noise = 0.0;
                        double ridge_weight = 1.0;
                        double ridge_total = 0.0;
                        double ridge_frequency = mountain_frequency;
                        for (std::uint32_t octave = 0; octave < mountain_octaves &&
                             ridge_frequency <= nyquist; ++octave, ridge_frequency *= 2.0,
                             ridge_weight *= 0.52)
                        {
                            const double noise = SampleTerrainPerlin(
                                wx * ridge_frequency, wz * ridge_frequency,
                                context.node_seed + 71 + octave);
                            const double ridge = 1.0 - std::abs(noise);
                            ridged_noise += ridge * ridge * ridge_weight;
                            ridge_total += ridge_weight;
                        }
                        ridged_noise = ridge_total > 0.0 ? ridged_noise / ridge_total : 0.0;
                        const double base_terrain = (static_cast<double>(detail[index]) * 2.0 - 1.0) *
                            (base_terrain_weight * landform_amplitude);
                        const double macro = macro_noise * (macro_weight * landform_amplitude);
                        const double mountain = mountain_mask * ridged_noise *
                            (mountain_weight * landform_amplitude);
                        const double height = base_height + base_terrain + macro + mountain -
                            edge_mask * coast_depth;
                        heights[index] = static_cast<float>(height);
                        edge_masks[index] = static_cast<float>(edge_mask);
                        mountains[index] = static_cast<float>(mountain_mask);
                    }
                }
                return OperatorOutputs{
                    {"height", field_from_samples(context, std::move(heights))},
                    {"edge_mask", field_from_samples(context, std::move(edge_masks))},
                    {"mountain_mask", field_from_samples(context, std::move(mountains))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.height_filter", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double minimum = p.value("minimum", 0.0);
                const double maximum = p.value("maximum", 1.0);
                if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
                    throw std::invalid_argument("height filter needs finite ordered bounds");
                const auto &source = inputs.at("source")->Samples();
                std::vector<float> samples(source.size());
                for (std::size_t i = 0; i < source.size(); ++i)
                {
                    double height = source[i];
                    if (height < 0.0) height = std::abs(height);
                    else if (height < minimum || height > maximum) height *= maximum - minimum;
                    samples[i] = static_cast<float>(height);
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.erosion.coastal_walk", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                if (!p.value("enabled", true))
                    return OperatorOutputs{{"height", inputs.at("source")}};
                const std::uint32_t erosion_units = p.value("erosion_units_per_edge", 2000u);
                const std::uint32_t initial_walkers = p.value("initial_walkers", 100u);
                const double erosion_depth = p.value("erosion_depth", 0.3);
                if (erosion_units > 1000000u || initial_walkers == 0 || initial_walkers > 4096 ||
                    !std::isfinite(erosion_depth) || erosion_depth < 0.0 || erosion_depth > 1.0)
                    throw std::invalid_argument("coastal walk needs bounded erosion units, walkers and depth");

                const auto &source = inputs.at("source")->Samples();
                const GridDomain2D &domain = context.domain;
                if (domain.width < 2 || domain.height < 2)
                    return OperatorOutputs{{"height", inputs.at("source")}};
                std::vector<float> samples = source;
                std::uint64_t random_state = context.node_seed;
                struct Walker { std::uint32_t x; std::uint32_t y; };
                const auto random_index = [&random_state](std::uint64_t count)
                { return static_cast<std::uint32_t>(NextTerrainRandom(random_state) % count); };
                for (std::uint32_t edge = 0; edge < 4; ++edge)
                {
                    std::vector<Walker> walkers;
                    walkers.reserve(initial_walkers);
                    for (std::uint32_t i = 0; i < initial_walkers; ++i)
                    {
                        const std::uint32_t cross_extent = (edge < 2) ? domain.width : domain.height;
                        const std::int64_t center = static_cast<std::int64_t>(cross_extent / 2);
                        const std::int64_t half = static_cast<std::int64_t>(cross_extent / 2);
                        const std::int64_t jitter = static_cast<std::int64_t>(random_index(
                            static_cast<std::uint64_t>(half * 2 + 1))) - half;
                        const auto cross = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                            center + jitter, 0, cross_extent - 1));
                        if (edge == 0) walkers.push_back({cross, 0});
                        else if (edge == 1) walkers.push_back({cross, domain.height - 1});
                        else if (edge == 2) walkers.push_back({domain.width - 1, cross});
                        else walkers.push_back({0, cross});
                    }
                    for (std::uint32_t step = 0; step < erosion_units && !walkers.empty(); ++step)
                    {
                        if ((step & 255u) == 0u && context.cancelled && context.cancelled->load())
                            throw std::runtime_error("evaluation cancelled");
                        Walker &walker = walkers.front();
                        const std::size_t index = static_cast<std::size_t>(walker.y) * domain.width + walker.x;
                        samples[index] = static_cast<float>(std::clamp(
                            static_cast<double>(samples[index]) - erosion_depth, 0.0, 1.0));
                        if (edge < 2)
                        {
                            const int direction = random_index(2) == 0 ? -1 : 1;
                            const int inward = random_index(2) == 0 ? 0 : (edge == 0 ? 1 : -1);
                            walker.x = static_cast<std::uint32_t>(std::clamp(
                                static_cast<int>(walker.x) + direction, 0, static_cast<int>(domain.width) - 1));
                            walker.y = static_cast<std::uint32_t>(std::clamp(
                                static_cast<int>(walker.y) + inward, 0, static_cast<int>(domain.height) - 1));
                        }
                        else
                        {
                            const int direction = random_index(2) == 0 ? -1 : 1;
                            const int inward = random_index(2) == 0 ? 0 : (edge == 2 ? -1 : 1);
                            walker.y = static_cast<std::uint32_t>(std::clamp(
                                static_cast<int>(walker.y) + direction, 0, static_cast<int>(domain.height) - 1));
                            walker.x = static_cast<std::uint32_t>(std::clamp(
                                static_cast<int>(walker.x) + inward, 0, static_cast<int>(domain.width) - 1));
                        }
                        std::rotate(walkers.begin(), walkers.begin() + 1, walkers.end());
                    }
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.fractal_perlin", 1, {}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &) {
                const double amplitude = p.at("amplitude_m").get<double>();
                const double base_frequency = p.at("frequency_per_m").get<double>();
                const std::uint32_t octaves = p.value("octaves", 6u);
                const double persistence = p.value("persistence", 0.35);
                const double lacunarity = p.value("lacunarity", 2.0);
                struct TerrainArea
                {
                    double center_x_m;
                    double center_z_m;
                    double radius_m;
                    double height_offset_m;
                    double amplitude_scale;
                    double persistence;
                };
                const auto regions = p.value("regions", nlohmann::json::array());
                const double nyquist = 0.5 / std::max(context.domain.spacing_x_m,
                                                     context.domain.spacing_z_m);
                if (!std::isfinite(amplitude) || amplitude < 0.0 ||
                    !std::isfinite(base_frequency) || base_frequency <= 0.0 || base_frequency > nyquist ||
                    octaves == 0 || octaves > 12 || !std::isfinite(persistence) ||
                    persistence < 0.0 || persistence > 1.0 ||
                    !std::isfinite(lacunarity) || lacunarity < 1.0 || lacunarity > 4.0 ||
                    !regions.is_array() || regions.size() > 32)
                    throw std::invalid_argument(
                        "fractal Perlin needs finite amplitude, a sampled base frequency, 1..12 octaves, "
                        "persistence in [0, 1], lacunarity in [1, 4] and at most 32 regions");

                std::vector<TerrainArea> areas;
                areas.reserve(regions.size());
                for (const auto &region : regions)
                {
                    const auto center = region.at("center_xz_m").get<std::array<double, 2>>();
                    const TerrainArea area{
                        center[0], center[1], region.at("radius_m").get<double>(),
                        region.value("height_offset_m", 0.0),
                        region.value("amplitude_scale", 1.0),
                        region.value("persistence", persistence)};
                    if (!std::isfinite(area.center_x_m) || !std::isfinite(area.center_z_m) ||
                        !std::isfinite(area.radius_m) || area.radius_m <= 0.0 ||
                        !std::isfinite(area.height_offset_m) ||
                        !std::isfinite(area.amplitude_scale) || area.amplitude_scale < 0.0 ||
                        !std::isfinite(area.persistence) || area.persistence < 0.0 ||
                        area.persistence > 1.0)
                        throw std::invalid_argument("terrain areas need finite centers, positive radii and valid noise scales");
                    areas.push_back(area);
                }

                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                {
                    if ((y & 15u) == 0u && context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        double region_weight = 0.0;
                        double region_height = 0.0;
                        double region_amplitude = 0.0;
                        double region_persistence = 0.0;
                        for (const TerrainArea &area : areas)
                        {
                            const double dx = (wx - area.center_x_m) / area.radius_m;
                            const double dz = (wz - area.center_z_m) / area.radius_m;
                            const double edge = std::clamp(1.0 - std::sqrt(dx * dx + dz * dz), 0.0, 1.0);
                            const double weight_at_area = edge * edge * (3.0 - 2.0 * edge);
                            region_weight += weight_at_area;
                            region_height += weight_at_area * area.height_offset_m;
                            region_amplitude += weight_at_area * area.amplitude_scale;
                            region_persistence += weight_at_area * area.persistence;
                        }
                        double local_height = 0.0;
                        double local_amplitude = amplitude;
                        double local_persistence = persistence;
                        if (region_weight > 0.0)
                        {
                            const double blend = std::min(region_weight, 1.0);
                            local_height = (region_height / region_weight) * blend;
                            local_amplitude *= 1.0 +
                                (region_amplitude / region_weight - 1.0) * blend;
                            local_persistence +=
                                (region_persistence / region_weight - persistence) * blend;
                        }
                        double frequency = base_frequency;
                        double weight = 1.0;
                        double total_weight = 0.0;
                        double noise_height = 0.0;
                        for (std::uint32_t octave = 0; octave < octaves && frequency <= nyquist;
                             ++octave, frequency *= lacunarity, weight *= local_persistence)
                        {
                            noise_height += SampleTerrainPerlin(
                                wx * frequency, wz * frequency,
                                context.node_seed + octave) * weight;
                            total_weight += weight;
                        }
                        samples[static_cast<std::size_t>(y) * context.domain.width + x] =
                            static_cast<float>(local_height + local_amplitude * noise_height / total_weight);
                    }
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.fractal_perlin_detail", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double amplitude = p.at("amplitude_m").get<double>();
                const double base_frequency = p.at("frequency_per_m").get<double>();
                const std::uint32_t octaves = p.value("octaves", 3u);
                const double persistence = p.value("persistence", 0.5);
                const double lacunarity = p.value("lacunarity", 2.0);
                const double nyquist = 0.5 / std::max(context.domain.spacing_x_m,
                                                     context.domain.spacing_z_m);
                if (!std::isfinite(amplitude) || amplitude < 0.0 ||
                    !std::isfinite(base_frequency) || base_frequency <= 0.0 || base_frequency > nyquist ||
                    octaves == 0 || octaves > 12 || !std::isfinite(persistence) ||
                    persistence < 0.0 || persistence > 1.0 ||
                    !std::isfinite(lacunarity) || lacunarity < 1.0 || lacunarity > 4.0)
                    throw std::invalid_argument(
                        "Perlin detail needs finite amplitude, a sampled base frequency, 1..12 octaves, "
                        "persistence in [0, 1] and lacunarity in [1, 4]");

                const auto &source = inputs.at("source")->Samples();
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                {
                    if ((y & 15u) == 0u && context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        double frequency = base_frequency;
                        double weight = 1.0;
                        double total_weight = 0.0;
                        double noise_height = 0.0;
                        for (std::uint32_t octave = 0; octave < octaves && frequency <= nyquist;
                             ++octave, frequency *= lacunarity, weight *= persistence)
                        {
                            noise_height += SampleTerrainPerlin(
                                wx * frequency, wz * frequency,
                                context.node_seed + octave) * weight;
                            total_weight += weight;
                        }
                        const std::size_t index = static_cast<std::size_t>(y) * context.domain.width + x;
                        samples[index] = static_cast<float>(source[index] +
                                                            amplitude * noise_height / total_weight);
                    }
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.ridge_curve", 1, {}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &) {
                const float base = p.value("base_m", static_cast<float>(context.domain.datum_y_m));
                const double width = p.at("width_m").get<double>();
                const float amplitude = p.at("amplitude_m").get<float>();
                const bool valley = p.value("valley", false);
                if (!std::isfinite(width) || width <= 0.0 || !std::isfinite(amplitude) || !std::isfinite(base))
                    throw std::invalid_argument("curve width must be positive and heights finite meters");
                const auto points = p.at("points_xz_m").get<std::vector<std::array<double, 2>>>();
                if (points.size() < 2) throw std::invalid_argument("ridge/valley curve needs two world-space points");
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples), base);
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        double distance_sq = std::numeric_limits<double>::infinity();
                        for (std::size_t i = 1; i < points.size(); ++i)
                        {
                            const double ax = points[i - 1][0], az = points[i - 1][1];
                            const double dx = points[i][0] - ax, dz = points[i][1] - az;
                            const double length_sq = dx * dx + dz * dz;
                            if (!std::isfinite(length_sq)) throw std::invalid_argument("curve coordinates must be finite");
                            const double t = length_sq > 0.0 ? std::clamp(((wx - ax) * dx + (wz - az) * dz) / length_sq, 0.0, 1.0) : 0.0;
                            const double ex = wx - (ax + t * dx), ez = wz - (az + t * dz);
                            distance_sq = std::min(distance_sq, ex * ex + ez * ez);
                        }
                        const double influence = std::exp(-0.5 * distance_sq / (width * width));
                        const double sign = valley ? -1.0 : 1.0;
                        samples[static_cast<std::size_t>(y) * context.domain.width + x] =
                            static_cast<float>(base + sign * amplitude * influence);
                    }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.domain_warp", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double amplitude = p.at("amplitude_m").get<double>();
                const double frequency = p.at("frequency_per_m").get<double>();
                if (!std::isfinite(amplitude) || amplitude < 0.0 || !std::isfinite(frequency) || frequency < 0.0)
                    throw std::invalid_argument("warp amplitude and frequency must be finite and nonnegative");
                const auto &source = *inputs.at("source");
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        const double limit = 0.5 / std::max(context.domain.spacing_x_m, context.domain.spacing_z_m);
                        const double f = std::min(frequency, limit);
                        const double px = wx + amplitude * SampleTerrainPerlin(
                            wx * f, wz * f, context.node_seed);
                        const double pz = wz + amplitude * SampleTerrainPerlin(
                            wx * f, wz * f, context.node_seed ^ 0x9e3779b97f4a7c15ull);
                        const auto gx = std::clamp((px - context.domain.origin_x_m) / context.domain.spacing_x_m, 0.0, static_cast<double>(context.domain.width - 1));
                        const auto gy = std::clamp((pz - context.domain.origin_z_m) / context.domain.spacing_z_m, 0.0, static_cast<double>(context.domain.height - 1));
                        const auto x0 = static_cast<std::uint32_t>(gx), y0 = static_cast<std::uint32_t>(gy);
                        const auto x1 = std::min(x0 + 1, context.domain.width - 1), y1 = std::min(y0 + 1, context.domain.height - 1);
                        const double tx = gx - x0, ty = gy - y0;
                        const auto at = [&source, &context](std::uint32_t ix, std::uint32_t iy) { return static_cast<double>(source.At(ix, iy)); };
                        samples[static_cast<std::size_t>(y) * context.domain.width + x] = static_cast<float>(
                            (1.0 - ty) * ((1.0 - tx) * at(x0, y0) + tx * at(x1, y0)) +
                            ty * ((1.0 - tx) * at(x0, y1) + tx * at(x1, y1)));
                    }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.ridged_detail", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double amplitude = p.at("amplitude_m").get<double>();
                const double frequency = p.at("frequency_per_m").get<double>();
                const std::uint32_t octaves = p.value("octaves", 4u);
                if (!std::isfinite(amplitude) || !std::isfinite(frequency) || amplitude < 0.0 || frequency <= 0.0 || octaves == 0 || octaves > 12)
                    throw std::invalid_argument("ridged detail needs finite amplitude, positive frequency and 1..12 octaves");
                const double nyquist = 0.5 / std::max(context.domain.spacing_x_m, context.domain.spacing_z_m);
                std::vector<float> samples = inputs.at("source")->Samples();
                for (std::uint32_t y = 0; y < context.domain.height; ++y)
                    for (std::uint32_t x = 0; x < context.domain.width; ++x)
                    {
                        const double wx = context.domain.origin_x_m + x * context.domain.spacing_x_m;
                        const double wz = context.domain.origin_z_m + y * context.domain.spacing_z_m;
                        double detail = 0.0, weight = 1.0, total = 0.0;
                        double f = frequency;
                        for (std::uint32_t octave = 0; octave < octaves && f <= nyquist; ++octave, f *= 2.0, weight *= 0.5)
                        {
                            const double n = SampleTerrainPerlin(
                                wx * f, wz * f, context.node_seed + octave);
                            detail += (1.0 - std::abs(n)) * weight;
                            total += weight;
                        }
                        if (total > 0.0) samples[static_cast<std::size_t>(y) * context.domain.width + x] += static_cast<float>(amplitude * (detail / total - 0.5));
                    }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.remap", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const auto &source = *inputs.at("source");
                auto samples = source.Samples();
                if (p.contains("curve_points"))
                {
                    const RemapCurve curve(p.at("curve_points").get<std::vector<std::array<double, 2>>>());
                    const bool normalize_input = p.value("normalize_input", true);
                    const auto [minimum, maximum] = std::minmax_element(samples.begin(), samples.end());
                    const double extent = static_cast<double>(*maximum) - *minimum;
                    if (normalize_input)
                    {
                        if (extent > 0.0)
                            for (float &sample : samples)
                            {
                                const double normalized = std::clamp((sample - *minimum) / extent, 0.0, 1.0);
                                sample = static_cast<float>(*minimum + curve.Evaluate(normalized) * extent);
                            }
                    }
                    else
                    {
                        for (float &sample : samples)
                            sample = static_cast<float>(curve.Evaluate(std::clamp(
                                static_cast<double>(sample), 0.0, 1.0)));
                    }
                }
                else
                {
                    const double in_min = p.at("input_min_m").get<double>(), in_max = p.at("input_max_m").get<double>();
                    const double out_min = p.at("output_min_m").get<double>(), out_max = p.at("output_max_m").get<double>();
                    if (!std::isfinite(in_min) || !std::isfinite(in_max) || !std::isfinite(out_min) || !std::isfinite(out_max) || in_max <= in_min)
                        throw std::invalid_argument("remap ranges must be finite and input range increasing");
                    for (auto &sample : samples)
                        sample = static_cast<float>(out_min + std::clamp((sample - in_min) / (in_max - in_min), 0.0, 1.0) * (out_max - out_min));
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.scale", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double scale = p.at("scale").get<double>();
                const double offset = p.value("offset_m", 0.0);
                if (!std::isfinite(scale) || scale < 0.0 || !std::isfinite(offset))
                    throw std::invalid_argument("height scale needs a nonnegative finite scale and finite offset");
                std::vector<float> samples = inputs.at("source")->Samples();
                for (float &sample : samples)
                    sample = static_cast<float>(offset + sample * scale);
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.erosion.talus_relax", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double repose_angle = p.at("repose_angle_radians").get<double>();
                const double transport_fraction = p.value("transport_fraction", 0.35);
                const std::uint32_t iterations = p.value("iterations", 24u);
                if (!std::isfinite(repose_angle) || repose_angle < 0.0 || repose_angle >= 1.5707963267948966 ||
                    !std::isfinite(transport_fraction) || transport_fraction < 0.0 || transport_fraction > 1.0 ||
                    iterations == 0 || iterations > 512)
                    throw std::invalid_argument("talus erosion needs a valid repose angle, transport fraction and 1..512 iterations");

                const GridDomain2D &domain = context.domain;
                std::vector<double> heights(inputs.at("source")->Samples().begin(),
                    inputs.at("source")->Samples().end());
                const double slope_limit = std::tan(repose_angle);
                const double edge_rate = transport_fraction / 16.0;
                std::vector<double> delta(heights.size(), 0.0);
                for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
                {
                    if (context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    std::fill(delta.begin(), delta.end(), 0.0);
                    for (std::uint32_t y = 0; y < domain.height; ++y)
                        for (std::uint32_t x = 0; x < domain.width; ++x)
                        {
                            const std::size_t here = static_cast<std::size_t>(y) * domain.width + x;
                            for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1,0},{0,1},{1,1},{-1,1}}})
                            {
                                const int nx = static_cast<int>(x) + dx;
                                const int ny = static_cast<int>(y) + dy;
                                if (nx < 0 || ny < 0 || nx >= static_cast<int>(domain.width) || ny >= static_cast<int>(domain.height))
                                    continue;
                                const std::size_t there = static_cast<std::size_t>(ny) * domain.width + static_cast<std::uint32_t>(nx);
                                const double distance = std::hypot(dx * domain.spacing_x_m, dy * domain.spacing_z_m);
                                const double difference = heights[here] - heights[there];
                                const double excess = std::abs(difference) - slope_limit * distance;
                                if (excess <= 0.0) continue;
                                const double amount = edge_rate * excess;
                                if (difference > 0.0)
                                {
                                    delta[here] -= amount;
                                    delta[there] += amount;
                                }
                                else
                                {
                                    delta[there] -= amount;
                                    delta[here] += amount;
                                }
                            }
                        }
                    for (std::size_t i = 0; i < heights.size(); ++i)
                        heights[i] += delta[i];
                }
                std::vector<float> samples(heights.size());
                std::transform(heights.begin(), heights.end(), samples.begin(),
                    [](double value) { return static_cast<float>(value); });
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.erosion.thermal_flux", 1,
            {{"source", PortType::Heightfield}}, {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double talus_angle_degrees = p.value("talus_angle_degrees", 30.0);
                const double thermal_rate = p.value("thermal_rate", 0.25);
                const std::uint32_t iterations = p.value("iterations", 48u);
                if (!std::isfinite(talus_angle_degrees) || talus_angle_degrees < 0.0 ||
                    talus_angle_degrees >= 89.0 || !std::isfinite(thermal_rate) ||
                    thermal_rate <= 0.0 || thermal_rate > 1.0 ||
                    iterations == 0 || iterations > 512)
                    throw std::invalid_argument(
                        "thermal flux needs TalusAngle in [0, 89), ThermalRate in (0, 1], and 1..512 iterations");

                constexpr std::array<std::pair<int, int>, 8> neighbor_offsets{{
                    {-1, -1}, {0, -1}, {1, -1}, {1, 0},
                    {1, 1}, {0, 1}, {-1, 1}, {-1, 0}}};
                constexpr std::array<std::size_t, 8> opposite_directions{{4, 5, 6, 7, 0, 1, 2, 3}};
                const GridDomain2D &domain = context.domain;
                std::vector<float> heights = inputs.at("source")->Samples();
                std::vector<float> next_heights(heights.size());
                std::vector<std::array<float, 8>> flux(heights.size());
                std::vector<double> excess(8);
                const double slope_limit = std::tan(
                    talus_angle_degrees * 0.01745329251994329577);
                std::array<double, 8> talus_thresholds{};
                for (std::size_t direction = 0; direction < neighbor_offsets.size(); ++direction)
                {
                    const auto [dx, dy] = neighbor_offsets[direction];
                    talus_thresholds[direction] = slope_limit * std::hypot(
                        dx * domain.spacing_x_m, dy * domain.spacing_z_m);
                }
                for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
                {
                    if (context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");

                    // Pass one: each cell writes only its own eight outgoing flux slots.
                    for (std::uint32_t y = 0; y < domain.height; ++y)
                        for (std::uint32_t x = 0; x < domain.width; ++x)
                        {
                            const std::size_t here = static_cast<std::size_t>(y) * domain.width + x;
                            auto &cell_flux = flux[here];
                            cell_flux.fill(0.0f);
                            double total_excess = 0.0;
                            double maximum_excess = 0.0;
                            for (std::size_t direction = 0; direction < neighbor_offsets.size(); ++direction)
                            {
                                const auto [dx, dy] = neighbor_offsets[direction];
                                const int nx = static_cast<int>(x) + dx;
                                const int ny = static_cast<int>(y) + dy;
                                if (nx < 0 || ny < 0 || nx >= static_cast<int>(domain.width) ||
                                    ny >= static_cast<int>(domain.height))
                                {
                                    excess[direction] = 0.0;
                                    continue;
                                }
                                const std::size_t there = static_cast<std::size_t>(ny) * domain.width +
                                    static_cast<std::uint32_t>(nx);
                                const double amount = std::max(0.0,
                                    static_cast<double>(heights[here]) - heights[there] -
                                        talus_thresholds[direction]);
                                excess[direction] = amount;
                                total_excess += amount;
                                maximum_excess = std::max(maximum_excess, amount);
                            }
                            if (total_excess <= 0.0) continue;

                            // Limit one cell's transport to its steepest excess so the
                            // gather pass cannot overshoot a local repose threshold.
                            const double moved = thermal_rate * std::min(total_excess, maximum_excess);
                            for (std::size_t direction = 0; direction < neighbor_offsets.size(); ++direction)
                                cell_flux[direction] = static_cast<float>(moved * excess[direction] / total_excess);
                        }

                    // Pass two: gather neighbor fluxes into a distinct destination field.
                    for (std::uint32_t y = 0; y < domain.height; ++y)
                        for (std::uint32_t x = 0; x < domain.width; ++x)
                        {
                            const std::size_t here = static_cast<std::size_t>(y) * domain.width + x;
                            double net_change = 0.0;
                            for (std::size_t direction = 0; direction < neighbor_offsets.size(); ++direction)
                            {
                                const auto [dx, dy] = neighbor_offsets[direction];
                                const int nx = static_cast<int>(x) + dx;
                                const int ny = static_cast<int>(y) + dy;
                                net_change -= flux[here][direction];
                                if (nx < 0 || ny < 0 || nx >= static_cast<int>(domain.width) ||
                                    ny >= static_cast<int>(domain.height))
                                    continue;
                                const std::size_t there = static_cast<std::size_t>(ny) * domain.width +
                                    static_cast<std::uint32_t>(nx);
                                net_change += flux[there][opposite_directions[direction]];
                            }
                            next_heights[here] = static_cast<float>(heights[here] + net_change);
                        }
                    heights.swap(next_heights);
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(heights))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.erosion.stream_power_incision", 1,
            {{"source", PortType::Heightfield}},
            {{"height", PortType::Heightfield}, {"incision_m", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double erodibility = p.at("erodibility_m_per_iteration").get<double>();
                const double area_exponent = p.value("area_exponent", 0.5);
                const double maximum_incision = p.at("maximum_incision_m_per_iteration").get<double>();
                const std::uint32_t iterations = p.value("iterations", 8u);
                if (!std::isfinite(erodibility) || erodibility < 0.0 ||
                    !std::isfinite(area_exponent) || area_exponent < 0.0 || area_exponent > 1.0 ||
                    !std::isfinite(maximum_incision) || maximum_incision <= 0.0 ||
                    iterations == 0 || iterations > 256)
                    throw std::invalid_argument("stream-power incision needs nonnegative erodibility, area exponent in [0, 1], positive incision cap and 1..256 iterations");

                const GridDomain2D &domain = context.domain;
                std::vector<float> heights = inputs.at("source")->Samples();
                std::vector<float> cumulative_incision(heights.size(), 0.0f);
                for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
                {
                    if (context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    const auto current = field_from_samples(context, heights);
                    const DrainageNetwork drainage = RouteDrainage(
                        *current, DrainageOutletPolicy::Perimeter);
                    const double cell_count = static_cast<double>(heights.size());
                    for (auto order = drainage.flood_order.rbegin(); order != drainage.flood_order.rend(); ++order)
                    {
                        const std::size_t index = *order;
                        const std::uint32_t downstream = drainage.downstream[index];
                        if (downstream == DrainageNetwork::NoDownstream) continue;
                        const std::uint32_t x = static_cast<std::uint32_t>(index % domain.width);
                        const std::uint32_t y = static_cast<std::uint32_t>(index / domain.width);
                        const std::uint32_t next_x = downstream % domain.width;
                        const std::uint32_t next_y = downstream / domain.width;
                        const double distance = std::hypot(
                            static_cast<double>(static_cast<int>(x) - static_cast<int>(next_x)) * domain.spacing_x_m,
                            static_cast<double>(static_cast<int>(y) - static_cast<int>(next_y)) * domain.spacing_z_m);
                        const double area_fraction = std::clamp(
                            drainage.accumulation_cells[index] / cell_count, 0.0, 1.0);
                        const double downstream_height = heights[downstream];
                        const double current_height = heights[index];
                        if (distance <= 0.0 || current_height <= downstream_height) continue;

                        // For slope exponent one, this is the bounded implicit
                        // update of the local stream-power incision equation.
                        const double coefficient = erodibility *
                            std::pow(area_fraction, area_exponent) / distance;
                        const double solved = (current_height + coefficient * downstream_height) /
                            (1.0 + coefficient);
                        const double incision = std::clamp(current_height - solved,
                            0.0, maximum_incision);
                        heights[index] = static_cast<float>(current_height - incision);
                        cumulative_incision[index] += static_cast<float>(incision);
                    }
                }
                return OperatorOutputs{
                    {"height", field_from_samples(context, std::move(heights))},
                    {"incision_m", field_from_samples(context, std::move(cumulative_incision))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.heightfield.blend", 1,
            {{"base", PortType::Heightfield}, {"detail", PortType::Heightfield}, {"weight", PortType::ScalarField2D}},
            {{"height", PortType::Heightfield}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const double scale = p.value("detail_scale", 1.0);
                if (!std::isfinite(scale)) throw std::invalid_argument("blend detail scale must be finite");
                std::vector<float> samples(context.domain.SampleCount(context.maximum_samples));
                for (std::size_t i = 0; i < samples.size(); ++i)
                {
                    const double weight = std::clamp(static_cast<double>(inputs.at("weight")->Samples()[i]), 0.0, 1.0);
                    samples[i] = static_cast<float>(inputs.at("base")->Samples()[i] * (1.0 - weight) + inputs.at("detail")->Samples()[i] * scale * weight);
                }
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.field.slope", 1, {{"height", PortType::Heightfield}},
            {{"slope_radians", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &,
                                 const OperatorInputs &inputs) {
                return OperatorOutputs{{"slope_radians", field_from_samples(context,
                    ComputeSlopeRadians(RequireScalarField(inputs.at("height"), "height")))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.field.curvature", 1, {{"height", PortType::Heightfield}},
            {{"curvature_per_m", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &,
                                 const OperatorInputs &inputs) {
                return OperatorOutputs{{"curvature_per_m", field_from_samples(context,
                    ComputeCurvaturePerMeter(RequireScalarField(inputs.at("height"), "height")))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.drainage.accumulation", 1, {{"height", PortType::Heightfield}},
            {{"accumulation_cells", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const auto policy = p.value("authored_lakes", false) ? DrainageOutletPolicy::AuthoredLakesAndPerimeter : DrainageOutletPolicy::Perimeter;
                std::vector<std::uint8_t> lakes;
                if (p.contains("lake_mask")) lakes = p.at("lake_mask").get<std::vector<std::uint8_t>>();
                const auto routed = RouteDrainage(
                    RequireScalarField(inputs.at("height"), "height"), policy, lakes);
                std::vector<float> samples(routed.accumulation_cells.size());
                std::transform(routed.accumulation_cells.begin(), routed.accumulation_cells.end(), samples.begin(),
                    [](double value) { return static_cast<float>(value); });
                return OperatorOutputs{{"accumulation_cells", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        return RegisterTerrainHydraulicOperators(*this, diagnostic);
    }

    TerrainEvaluator::TerrainEvaluator(std::shared_ptr<const OperatorRegistry> registry,
                                       EvaluationOptions options)
        : registry_(std::move(registry)), options_(options)
    {
        if (!registry_) throw std::invalid_argument("terrain evaluator requires an operator registry");
    }

    EvaluationResult TerrainEvaluator::Evaluate(const TerrainRecipe &recipe,
        const std::atomic_bool *cancelled, EvaluationExecutionControl *control,
        const EvaluationProgressCallback &progress)
    {
        EvaluationResult result;
        const auto evaluation_started = std::chrono::steady_clock::now();
        const auto update_elapsed = [&result, evaluation_started]()
        {
            result.evaluation_time_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - evaluation_started).count();
        };
        try
        {
            recipe.domain.SampleCount(options_.maximum_samples);
            if (recipe.nodes.size() > options_.maximum_nodes)
                throw std::length_error("recipe exceeds configured node budget");
            std::map<std::string, const RecipeNode *, std::less<>> nodes;
            for (const auto &node : recipe.nodes)
            {
                if (node.id.empty() || !node.parameters.is_object() || !nodes.emplace(node.id, &node).second)
                    throw std::invalid_argument("recipe node IDs must be non-empty and unique");
                if (!registry_->Find(node.operator_id, node.operator_version))
                    throw std::invalid_argument("recipe references an unknown operator or version: " + node.operator_id);
            }

            std::map<std::string, std::size_t, std::less<>> indegree;
            std::map<std::string, std::vector<std::string>, std::less<>> dependents;
            for (const auto &[id, node] : nodes) indegree[id] = 0;
            for (const auto &[id, node] : nodes)
            {
                const auto *descriptor = registry_->Find(node->operator_id, node->operator_version);
                for (const auto &[port, type] : descriptor->inputs)
                {
                    const auto edge = node->inputs.find(port);
                    if (edge == node->inputs.end()) throw std::invalid_argument("required input port is missing: " + port);
                    auto source = nodes.find(edge->second.node);
                    if (source == nodes.end()) throw std::invalid_argument("input references a missing node");
                    const auto *source_descriptor = registry_->Find(source->second->operator_id, source->second->operator_version);
                    auto output = source_descriptor->outputs.find(edge->second.port);
                    if (output == source_descriptor->outputs.end() || output->second != type)
                        throw std::invalid_argument("incompatible or missing input port: " + port);
                    ++indegree[id];
                    dependents[edge->second.node].push_back(id);
                }
                for (const auto &[port, edge] : node->inputs)
                    if (!descriptor->inputs.contains(port)) throw std::invalid_argument("unknown input port: " + port);
            }

            std::set<std::string, std::less<>> ready;
            for (const auto &[id, count] : indegree) if (count == 0) ready.insert(id);
            std::vector<std::string> order;
            while (!ready.empty())
            {
                auto it = ready.begin();
                const auto id = *it;
                ready.erase(it);
                order.push_back(id);
                for (const auto &dependent : dependents[id])
                    if (--indegree[dependent] == 0) ready.insert(dependent);
            }
            if (order.size() != nodes.size()) throw std::invalid_argument("recipe graph contains a cycle");

            std::size_t retained_result_bytes = 0;
            for (const auto &id : order)
            {
                if (cancelled && cancelled->load())
                {
                    result.cancelled = true;
                    result.diagnostic = "evaluation cancelled";
                    update_elapsed();
                    return result;
                }
                if (control && !control->WaitForNode(cancelled))
                {
                    result.cancelled = true;
                    result.diagnostic = "evaluation cancelled";
                    update_elapsed();
                    return result;
                }
                const auto &node = *nodes.at(id);
                const auto *descriptor = registry_->Find(node.operator_id, node.operator_version);
                OperatorInputs inputs;
                for (const auto &[port, source] : node.inputs)
                {
                    const auto &source_node = result.nodes.at(source.node);
                    inputs.emplace(port, source_node.outputs.at(source.port));
                }
                const auto key = NodeKey(recipe, node, inputs);
                auto cached = cache_.find(key);
                if (cached != cache_.end())
                {
                    auto reused = cached->second.result;
                    reused.cache_hit = true;
                    reused.evaluation_time_ms = 0.0;
                    const auto [entry, inserted] = result.nodes.emplace(id, std::move(reused));
                    if (inserted && progress)
                    {
                        try { progress(id, entry->second); } catch (...) {}
                    }
                    continue;
                }
                OperatorContext context{recipe.domain, recipe.seed, options_.maximum_samples,
                                        NodeSeed(recipe, node), cancelled};
                const auto node_started = std::chrono::steady_clock::now();
                auto outputs = descriptor->evaluate(context, node.parameters, inputs);
                if (outputs.size() != descriptor->outputs.size()) throw std::runtime_error("operator returned an invalid output set");
                NodeResult node_result;
                node_result.content_hash = kFnvOffset;
                node_result.minimum_value = std::numeric_limits<float>::infinity();
                node_result.maximum_value = -std::numeric_limits<float>::infinity();
                for (const auto &[port, ignored_type] : descriptor->outputs)
                {
                    auto output = outputs.find(port);
                    if (output == outputs.end() || !output->second ||
                        !(output->second->Domain() == recipe.domain))
                        throw std::runtime_error("operator returned an invalid typed output: " + port);
                    const TerrainValueKind expected_kind = ignored_type == PortType::LayeredHeightfield2D
                        ? TerrainValueKind::LayeredHeightfield2D
                        : TerrainValueKind::ScalarField2D;
                    if (output->second->Kind() != expected_kind)
                        throw std::runtime_error("operator returned an output with the wrong terrain value kind: " + port);
                    const auto value_hash = HashTerrainValue(*output->second);
                    HashString(node_result.content_hash, port);
                    HashBytes(node_result.content_hash, &value_hash, sizeof(value_hash));
                    for (const float value : output->second->Samples())
                    {
                        node_result.all_values_finite =
                            node_result.all_values_finite && std::isfinite(value);
                        if (std::isfinite(value))
                        {
                            node_result.minimum_value = std::min(node_result.minimum_value, value);
                            node_result.maximum_value = std::max(node_result.maximum_value, value);
                        }
                    }
                    node_result.outputs.emplace(port, output->second);
                }
                std::size_t bytes = 0;
                std::set<const TerrainValue2D *> counted_outputs;
                for (const auto &[port, value] : node_result.outputs)
                {
                    (void)port;
                    if (counted_outputs.insert(value.get()).second)
                        bytes += value->ByteSize();
                }
                node_result.output_bytes = bytes;
                node_result.evaluation_time_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - node_started).count();
                if (!std::isfinite(node_result.minimum_value)) node_result.minimum_value = 0.0f;
                if (!std::isfinite(node_result.maximum_value)) node_result.maximum_value = 0.0f;
                if (bytes > options_.maximum_result_bytes ||
                    retained_result_bytes > options_.maximum_result_bytes - bytes)
                    throw std::length_error("evaluation exceeds configured result memory budget");
                retained_result_bytes += bytes;
                const auto [entry, inserted] = result.nodes.emplace(id, node_result);
                if (inserted && progress)
                {
                    try { progress(id, entry->second); } catch (...) {}
                }
                if (options_.maximum_cached_nodes && bytes <= options_.maximum_cache_bytes)
                {
                    while (!cache_.empty() && (cache_.size() >= options_.maximum_cached_nodes ||
                           cache_bytes_ + bytes > options_.maximum_cache_bytes))
                    {
                        cache_bytes_ -= cache_.begin()->second.bytes;
                        cache_.erase(cache_.begin());
                    }
                    cache_.emplace(key, CacheEntry{std::move(node_result), bytes});
                    cache_bytes_ += bytes;
                }
            }
            result.succeeded = true;
            update_elapsed();
            return result;
        }
        catch (const std::exception &error)
        {
            result.cancelled = cancelled && cancelled->load();
            result.diagnostic = error.what();
            update_elapsed();
            return result;
        }
    }

    void TerrainEvaluator::ClearCache()
    {
        cache_.clear();
        cache_bytes_ = 0;
    }

    GenerationExecutor::GenerationExecutor(std::shared_ptr<const OperatorRegistry> registry,
        std::size_t worker_count, std::size_t pending_capacity, std::size_t result_capacity,
        EvaluationOptions options)
        : registry_(std::move(registry)), options_(options),
          pending_capacity_(pending_capacity), result_capacity_(result_capacity)
    {
        if (!registry_ || worker_count == 0 || pending_capacity == 0 || result_capacity == 0)
            throw std::invalid_argument("generation executor requires registry, workers and bounded queues");
        workers_.reserve(worker_count);
        for (std::size_t i = 0; i < worker_count; ++i)
            workers_.emplace_back([this] { WorkerLoop(); });
    }

    GenerationExecutor::~GenerationExecutor()
    {
        Shutdown();
    }

    bool GenerationExecutor::Submit(std::uint64_t revision, TerrainRecipe recipe,
                                    std::shared_ptr<EvaluationExecutionControl> control,
                                    EvaluationProgressCallback progress)
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || revision < minimum_revision_ || pending_.size() >= pending_capacity_)
            return false;
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        for (auto &[active_revision, active] : active_)
            if (active_revision < revision) active->store(true);
        for (auto &job : pending_) job.cancelled->store(true);
        pending_.clear();
        minimum_revision_ = revision;
        pending_.push_back({revision, std::move(recipe), std::move(cancelled),
                            std::move(control), std::move(progress)});
        wake_.notify_one();
        return true;
    }

    bool GenerationExecutor::TryPop(GenerationJobResult &result)
    {
        std::lock_guard lock(mutex_);
        if (completed_.empty()) return false;
        result = std::move(completed_.front());
        completed_.pop_front();
        return true;
    }

    void GenerationExecutor::CancelBefore(std::uint64_t revision)
    {
        std::lock_guard lock(mutex_);
        minimum_revision_ = std::max(minimum_revision_, revision);
        for (auto &job : pending_)
            if (job.revision < minimum_revision_) job.cancelled->store(true);
        for (auto &[active_revision, active] : active_)
            if (active_revision < minimum_revision_) active->store(true);
        std::erase_if(completed_, [this](const auto &item) {
            return item.revision < minimum_revision_;
        });
    }

    void GenerationExecutor::Shutdown() noexcept
    {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
            for (auto &job : pending_) job.cancelled->store(true);
            for (auto &[revision, active] : active_) active->store(true);
            pending_.clear();
        }
        wake_.notify_all();
        for (auto &worker : workers_)
            if (worker.joinable()) worker.join();
        std::lock_guard lock(mutex_);
        completed_.clear();
        active_.clear();
    }

    void GenerationExecutor::WorkerLoop()
    {
        TerrainEvaluator evaluator(registry_, options_);
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
                if (stopping_ && pending_.empty()) return;
                job = std::move(pending_.front());
                pending_.pop_front();
                active_.emplace_back(job.revision, job.cancelled);
            }
            GenerationJobResult completed;
            completed.revision = job.revision;
            completed.evaluation = evaluator.Evaluate(job.recipe, job.cancelled.get(),
                                                       job.control.get(), job.progress);
            {
                std::lock_guard lock(mutex_);
                std::erase_if(active_, [&job](const auto &active) {
                    return active.second == job.cancelled;
                });
                if (!stopping_ && job.revision >= minimum_revision_ &&
                    !job.cancelled->load() && result_capacity_ > 0)
                {
                    while (completed_.size() >= result_capacity_) completed_.pop_front();
                    completed_.push_back(std::move(completed));
                }
            }
        }
    }
}
