#include "terrain_generation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

#include "math/noise2d.h"

namespace kpengine::terrain
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

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

        std::uint64_t HashField(const ScalarField2D &field)
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
            for (float sample : field.Samples())
            {
                const auto bits = std::bit_cast<std::uint32_t>(sample);
                HashBytes(hash, &bits, sizeof(bits));
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
                const auto input_hash = HashField(*field);
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
                        const double px = wx + amplitude * math::PerlinNoise2D(wx * f, wz * f, context.node_seed);
                        const double pz = wz + amplitude * math::PerlinNoise2D(wx * f, wz * f, context.node_seed ^ 0x9e3779b97f4a7c15ull);
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
                            const double n = math::PerlinNoise2D(wx * f, wz * f, context.node_seed + octave);
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
                const double in_min = p.at("input_min_m").get<double>(), in_max = p.at("input_max_m").get<double>();
                const double out_min = p.at("output_min_m").get<double>(), out_max = p.at("output_max_m").get<double>();
                if (!std::isfinite(in_min) || !std::isfinite(in_max) || !std::isfinite(out_min) || !std::isfinite(out_max) || in_max <= in_min)
                    throw std::invalid_argument("remap ranges must be finite and input range increasing");
                auto samples = inputs.at("source")->Samples();
                for (auto &sample : samples) sample = static_cast<float>(out_min + std::clamp((sample - in_min) / (in_max - in_min), 0.0, 1.0) * (out_max - out_min));
                return OperatorOutputs{{"height", field_from_samples(context, std::move(samples))}};
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
                return OperatorOutputs{{"slope_radians", field_from_samples(context, ComputeSlopeRadians(*inputs.at("height")))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.field.curvature", 1, {{"height", PortType::Heightfield}},
            {{"curvature_per_m", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &,
                                 const OperatorInputs &inputs) {
                return OperatorOutputs{{"curvature_per_m", field_from_samples(context, ComputeCurvaturePerMeter(*inputs.at("height")))}};
            }}, diagnostic)) return false;

        if (!Register({"terrain.drainage.accumulation", 1, {{"height", PortType::Heightfield}},
            {{"accumulation_cells", PortType::ScalarField2D}},
            [field_from_samples](const OperatorContext &context, const nlohmann::json &p,
                                 const OperatorInputs &inputs) {
                const auto policy = p.value("authored_lakes", false) ? DrainageOutletPolicy::AuthoredLakesAndPerimeter : DrainageOutletPolicy::Perimeter;
                std::vector<std::uint8_t> lakes;
                if (p.contains("lake_mask")) lakes = p.at("lake_mask").get<std::vector<std::uint8_t>>();
                const auto routed = RouteDrainage(*inputs.at("height"), policy, lakes);
                std::vector<float> samples(routed.accumulation_cells.size());
                std::transform(routed.accumulation_cells.begin(), routed.accumulation_cells.end(), samples.begin(),
                    [](double value) { return static_cast<float>(value); });
                return OperatorOutputs{{"accumulation_cells", field_from_samples(context, std::move(samples))}};
            }}, diagnostic)) return false;

        return true;
    }

    TerrainEvaluator::TerrainEvaluator(std::shared_ptr<const OperatorRegistry> registry,
                                       EvaluationOptions options)
        : registry_(std::move(registry)), options_(options)
    {
        if (!registry_) throw std::invalid_argument("terrain evaluator requires an operator registry");
    }

    EvaluationResult TerrainEvaluator::Evaluate(const TerrainRecipe &recipe,
                                                 const std::atomic_bool *cancelled)
    {
        EvaluationResult result;
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
                    result.nodes.emplace(id, std::move(reused));
                    continue;
                }
                OperatorContext context{recipe.domain, recipe.seed, options_.maximum_samples,
                                        NodeSeed(recipe, node), cancelled};
                auto outputs = descriptor->evaluate(context, node.parameters, inputs);
                if (outputs.size() != descriptor->outputs.size()) throw std::runtime_error("operator returned an invalid output set");
                NodeResult node_result;
                for (const auto &[port, ignored_type] : descriptor->outputs)
                {
                    auto output = outputs.find(port);
                    if (output == outputs.end() || !output->second ||
                        !(output->second->Domain() == recipe.domain))
                        throw std::runtime_error("operator returned an invalid typed output: " + port);
                    node_result.content_hash ^= HashField(*output->second);
                    node_result.outputs.emplace(port, output->second);
                }
                const auto bytes = std::accumulate(node_result.outputs.begin(), node_result.outputs.end(),
                    std::size_t{0}, [](std::size_t size, const auto &entry) { return size + entry.second->ByteSize(); });
                if (bytes > options_.maximum_result_bytes ||
                    retained_result_bytes > options_.maximum_result_bytes - bytes)
                    throw std::length_error("evaluation exceeds configured result memory budget");
                retained_result_bytes += bytes;
                result.nodes.emplace(id, node_result);
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
            return result;
        }
        catch (const std::exception &error)
        {
            result.cancelled = cancelled && cancelled->load();
            result.diagnostic = error.what();
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

    bool GenerationExecutor::Submit(std::uint64_t revision, TerrainRecipe recipe)
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
        pending_.push_back({revision, std::move(recipe), std::move(cancelled)});
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
            completed.evaluation = evaluator.Evaluate(job.recipe, job.cancelled.get());
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
