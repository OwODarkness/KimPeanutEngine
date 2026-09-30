#include "terrain_hydraulic_erosion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "terrain_generation.h"

namespace kpengine::terrain
{
    namespace
    {
        std::shared_ptr<const ScalarField2D> MakeField(
            const OperatorContext &context, std::vector<float> samples)
        {
            std::string diagnostic;
            auto field = ScalarField2D::Create(context.domain, std::move(samples),
                context.maximum_samples, diagnostic);
            if (!field) throw std::runtime_error(diagnostic);
            return field;
        }

        const ScalarField2D &RequireScalar(const OperatorInputs &inputs,
                                           const char *name)
        {
            const auto &value = inputs.at(name);
            const auto *field = value ? value->AsScalarField() : nullptr;
            if (field == nullptr)
                throw std::invalid_argument(std::string("hydraulic input must be a scalar field: ") + name);
            return *field;
        }

        struct Edge
        {
            int dx;
            int dy;
            double length_m;
            double face_width_m;
        };

        constexpr std::array<Edge, 4> kEdges{{
            {1, 0, 0.0, 0.0}, {-1, 0, 0.0, 0.0},
            {0, 1, 0.0, 0.0}, {0, -1, 0.0, 0.0}}};
    }

    bool RegisterTerrainHydraulicOperators(OperatorRegistry &registry,
                                            std::string &diagnostic)
    {
        if (!registry.Register({"terrain.state.layered_heightfield", 1,
            {{"bedrock_elevation_m", PortType::Heightfield},
             {"soil_thickness_m", PortType::ScalarField2D},
             {"sand_thickness_m", PortType::ScalarField2D},
             {"water_depth_m", PortType::ScalarField2D},
             {"suspended_sediment_kg_per_m2", PortType::ScalarField2D}},
            {{"state", PortType::LayeredHeightfield2D},
             {"surface_height_m", PortType::Heightfield}},
            [](const OperatorContext &context, const nlohmann::json &,
               const OperatorInputs &inputs) {
                const auto &bedrock = RequireScalar(inputs, "bedrock_elevation_m");
                const auto &soil = RequireScalar(inputs, "soil_thickness_m");
                const auto &sand = RequireScalar(inputs, "sand_thickness_m");
                const auto &water = RequireScalar(inputs, "water_depth_m");
                const auto &suspended = RequireScalar(inputs, "suspended_sediment_kg_per_m2");
                std::string error;
                auto state = LayeredHeightfield2D::Create(context.domain,
                    bedrock.Samples(), soil.Samples(), sand.Samples(), water.Samples(),
                    suspended.Samples(), context.maximum_samples, error);
                if (!state) throw std::invalid_argument(error);
                auto surface = MakeField(context, state->Samples());
                return OperatorOutputs{
                    {"state", std::move(state)},
                    {"surface_height_m", std::move(surface)}};
            }}, diagnostic)) return false;

        return registry.Register({"terrain.erosion.hydraulic_pipe", 1,
            {{"state", PortType::LayeredHeightfield2D},
             {"rain_rate_m_per_s", PortType::ScalarField2D},
             {"erodibility_0_1", PortType::ScalarField2D},
             {"hardness_0_1", PortType::ScalarField2D},
             {"obstacle_0_1", PortType::ScalarField2D}},
            {{"state", PortType::LayeredHeightfield2D},
             {"height", PortType::Heightfield},
             {"surface_height_m", PortType::Heightfield},
             {"bedrock_elevation_m", PortType::Heightfield},
             {"soil_thickness_m", PortType::ScalarField2D},
             {"sand_thickness_m", PortType::ScalarField2D},
             {"water_depth_m", PortType::ScalarField2D},
             {"suspended_sediment_kg_per_m2", PortType::ScalarField2D},
             {"flow_speed_m_per_s", PortType::ScalarField2D},
             {"discharge_m3_per_s", PortType::ScalarField2D},
             {"bedrock_eroded_kg_per_m2", PortType::ScalarField2D},
             {"soil_deposited_m", PortType::ScalarField2D},
             {"water_exported_m3", PortType::ScalarField2D},
             {"sediment_exported_kg", PortType::ScalarField2D},
             {"substep_count", PortType::ScalarField2D},
             {"simulated_time_s", PortType::ScalarField2D},
             {"water_budget_residual_m3", PortType::ScalarField2D},
             {"water_budget_relative_residual", PortType::ScalarField2D},
             {"solid_budget_residual_kg", PortType::ScalarField2D},
             {"solid_budget_relative_residual", PortType::ScalarField2D}},
            [](const OperatorContext &context, const nlohmann::json &p,
               const OperatorInputs &inputs) {
                const auto &state_value = inputs.at("state");
                const auto *state = state_value ? state_value->AsLayeredHeightfield() : nullptr;
                if (state == nullptr)
                    throw std::invalid_argument("hydraulic solver requires LayeredHeightfield2D state");
                const auto &rain = RequireScalar(inputs, "rain_rate_m_per_s").Samples();
                const auto &erodibility = RequireScalar(inputs, "erodibility_0_1").Samples();
                const auto &hardness = RequireScalar(inputs, "hardness_0_1").Samples();
                const auto &obstacles = RequireScalar(inputs, "obstacle_0_1").Samples();
                const auto &domain = context.domain;
                const std::size_t count = domain.SampleCount(context.maximum_samples);
                const double cell_area = domain.spacing_x_m * domain.spacing_z_m;
                const double minimum_spacing = std::min(domain.spacing_x_m, domain.spacing_z_m);

                const double duration = p.at("duration_s").get<double>();
                const double maximum_dt = p.at("maximum_timestep_s").get<double>();
                const double gravity = p.value("gravity_m_per_s2", 9.81);
                const double pipe_area = p.value("pipe_area_m2", 0.5);
                const double cfl = p.value("cfl", 0.35);
                const double capacity_coefficient = p.value("capacity_kg_s_per_m3", 2.0);
                const double dissolution_rate = p.value("dissolution_rate_per_s", 1.0);
                const double deposition_rate = p.value("deposition_rate_per_s", 1.0);
                const double soil_density = p.value("soil_bulk_density_kg_per_m3", 1600.0);
                const double sand_density = p.value("sand_bulk_density_kg_per_m3", 1700.0);
                const double bedrock_density = p.value("bedrock_density_kg_per_m3", 2700.0);
                const double bedrock_weathering_rate = p.value("bedrock_weathering_m_per_s", 0.0);
                const double maximum_bedrock_weathering = p.value("maximum_bedrock_weathering_m", 0.02);
                const double evaporation_rate = p.value("evaporation_rate_m_per_s", 0.0);
                const std::uint32_t maximum_substeps = p.value("maximum_substeps", 4096u);
                const std::size_t maximum_scratch_bytes = p.value(
                    "maximum_scratch_bytes", std::size_t{512} * 1024u * 1024u);
                const std::string boundary = p.value("boundary", std::string("closed"));
                if (!std::isfinite(duration) || duration <= 0.0 ||
                    !std::isfinite(maximum_dt) || maximum_dt <= 0.0 ||
                    !std::isfinite(gravity) || gravity <= 0.0 ||
                    !std::isfinite(pipe_area) || pipe_area <= 0.0 ||
                    !std::isfinite(cfl) || cfl <= 0.0 || cfl > 1.0 ||
                    !std::isfinite(capacity_coefficient) || capacity_coefficient < 0.0 ||
                    !std::isfinite(dissolution_rate) || dissolution_rate < 0.0 ||
                    !std::isfinite(deposition_rate) || deposition_rate < 0.0 ||
                    !std::isfinite(soil_density) || soil_density <= 0.0 ||
                    !std::isfinite(sand_density) || sand_density <= 0.0 ||
                    !std::isfinite(bedrock_density) || bedrock_density <= 0.0 ||
                    !std::isfinite(bedrock_weathering_rate) || bedrock_weathering_rate < 0.0 ||
                    !std::isfinite(maximum_bedrock_weathering) || maximum_bedrock_weathering < 0.0 ||
                    !std::isfinite(evaporation_rate) || evaporation_rate < 0.0 ||
                    maximum_substeps == 0 || maximum_substeps > 65536 ||
                    maximum_scratch_bytes == 0 ||
                    count > maximum_scratch_bytes / 256u ||
                    (boundary != "closed" && boundary != "open"))
                    throw std::invalid_argument("hydraulic parameters are outside their finite, nonnegative ranges");

                std::vector<double> bedrock(state->BedrockElevationMeters().begin(),
                    state->BedrockElevationMeters().end());
                std::vector<double> soil(state->SoilThicknessMeters().begin(),
                    state->SoilThicknessMeters().end());
                std::vector<double> sand(state->SandThicknessMeters().begin(),
                    state->SandThicknessMeters().end());
                std::vector<double> water(state->WaterDepthMeters().begin(),
                    state->WaterDepthMeters().end());
                std::vector<double> suspended(state->SuspendedSedimentKgPerSquareMeter().begin(),
                    state->SuspendedSedimentKgPerSquareMeter().end());
                std::vector<double> surface(count), flux(count * 4, 0.0);
                std::vector<double> next_water(count), next_suspended(count);
                std::vector<double> speed(count, 0.0), discharge(count, 0.0);
                std::vector<double> eroded_bedrock(count, 0.0), deposited_soil(count, 0.0);
                std::vector<double> exported_water(count, 0.0), exported_sediment(count, 0.0);
                std::vector<double> slope(count, 0.0);
                const auto &bedrock_input = state->BedrockElevationMeters();
                const auto &soil_input = state->SoilThicknessMeters();
                const auto &sand_input = state->SandThicknessMeters();
                double initial_water_volume = 0.0;
                double initial_solid_mass = 0.0;
                double rain_volume = 0.0;
                double evaporated_volume = 0.0;
                double exported_water_total = 0.0;
                double exported_sediment_total = 0.0;
                double bedrock_eroded_mass = 0.0;
                for (std::size_t i = 0; i < count; ++i)
                {
                    initial_water_volume += water[i] * cell_area;
                    initial_solid_mass += (soil[i] * soil_density + sand[i] * sand_density + suspended[i]) * cell_area;
                    if (!std::isfinite(rain[i]) || rain[i] < 0.0f ||
                        !std::isfinite(erodibility[i]) || erodibility[i] < 0.0f || erodibility[i] > 1.0f ||
                        !std::isfinite(hardness[i]) || hardness[i] < 0.0f || hardness[i] > 1.0f ||
                        !std::isfinite(obstacles[i]) || obstacles[i] < 0.0f || obstacles[i] > 1.0f)
                        throw std::invalid_argument("rain must be nonnegative; erodibility, hardness and obstacles must be in [0, 1]");
                }

                const auto edge_for = [&domain](std::size_t direction) {
                    auto edge = kEdges[direction];
                    edge.length_m = edge.dx != 0 ? domain.spacing_x_m : domain.spacing_z_m;
                    edge.face_width_m = edge.dx != 0 ? domain.spacing_z_m : domain.spacing_x_m;
                    return edge;
                };
                double elapsed = 0.0;
                std::uint32_t substep = 0;
                while (elapsed < duration)
                {
                    if (context.cancelled && context.cancelled->load())
                        throw std::runtime_error("evaluation cancelled");
                    if (++substep > maximum_substeps)
                        throw std::length_error("hydraulic solve exceeded maximum_substeps");

                    double maximum_water = 0.0;
                    for (const double depth : water) maximum_water = std::max(maximum_water, depth);
                    double maximum_rain = 0.0;
                    for (const float rate : rain) maximum_rain = std::max(maximum_rain,
                        static_cast<double>(rate));
                    const double wave_speed = std::sqrt(gravity *
                        (maximum_water + maximum_rain * maximum_dt));
                    const double stable_dt = wave_speed > 0.0
                        ? cfl * minimum_spacing / wave_speed : maximum_dt;
                    const double dt = std::min({maximum_dt, stable_dt, duration - elapsed});
                    if (!std::isfinite(dt) || dt <= 0.0)
                        throw std::runtime_error("hydraulic timestep collapsed to zero");

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        water[i] += rain[i] * dt;
                        rain_volume += rain[i] * dt * cell_area;
                        surface[i] = bedrock[i] + soil[i] + sand[i];
                    }
                    std::string field_error;
                    std::vector<float> surface_samples(count);
                    std::transform(surface.begin(), surface.end(), surface_samples.begin(),
                        [](double value) { return static_cast<float>(value); });
                    const auto surface_field = ScalarField2D::Create(domain,
                        std::move(surface_samples), context.maximum_samples, field_error);
                    if (!surface_field) throw std::runtime_error(field_error);
                    const auto slope_radians = ComputeSlopeRadians(*surface_field);
                    std::transform(slope_radians.begin(), slope_radians.end(), slope.begin(),
                        [](float value) { return static_cast<double>(value); });

                    std::fill(speed.begin(), speed.end(), 0.0);
                    std::fill(discharge.begin(), discharge.end(), 0.0);
                    for (std::uint32_t y = 0; y < domain.height; ++y)
                        for (std::uint32_t x = 0; x < domain.width; ++x)
                        {
                            const std::size_t source = static_cast<std::size_t>(y) * domain.width + x;
                            for (std::size_t direction = 0; direction < kEdges.size(); ++direction)
                            {
                                const Edge edge = edge_for(direction);
                                const int nx = static_cast<int>(x) + edge.dx;
                                const int ny = static_cast<int>(y) + edge.dy;
                                const bool outside = nx < 0 || ny < 0 ||
                                    nx >= static_cast<int>(domain.width) || ny >= static_cast<int>(domain.height);
                                const std::size_t slot = source * 4 + direction;
                                if (outside && boundary == "closed")
                                {
                                    flux[slot] = 0.0;
                                    continue;
                                }
                                const std::size_t target = outside ? source :
                                    static_cast<std::size_t>(ny) * domain.width + static_cast<std::uint32_t>(nx);
                                const double source_head = surface[source] + water[source];
                                const double target_head = outside ? domain.datum_y_m : surface[target] + water[target];
                                const double previous = flux[slot];
                                flux[slot] = std::max(0.0, previous + gravity * pipe_area * dt *
                                    (source_head - target_head) / edge.length_m);
                            }
                            const double outgoing = flux[source * 4] + flux[source * 4 + 1] +
                                flux[source * 4 + 2] + flux[source * 4 + 3];
                            const double available_volume = water[source] * cell_area;
                            const double scale = outgoing > 0.0
                                ? std::min(1.0, available_volume / (dt * outgoing)) : 1.0;
                            double signed_x = 0.0;
                            double signed_z = 0.0;
                            for (std::size_t direction = 0; direction < kEdges.size(); ++direction)
                            {
                                flux[source * 4 + direction] *= scale;
                                signed_x += flux[source * 4 + direction] * kEdges[direction].dx;
                                signed_z += flux[source * 4 + direction] * kEdges[direction].dy;
                                discharge[source] += flux[source * 4 + direction];
                            }
                            const double wet_x_area = std::max(water[source] * domain.spacing_z_m, 1.0e-12);
                            const double wet_z_area = std::max(water[source] * domain.spacing_x_m, 1.0e-12);
                            speed[source] = std::hypot(signed_x / wet_x_area, signed_z / wet_z_area);
                        }

                    std::copy(water.begin(), water.end(), next_water.begin());
                    std::copy(suspended.begin(), suspended.end(), next_suspended.begin());
                    for (std::size_t source = 0; source < count; ++source)
                    {
                        const double concentration = water[source] > 1.0e-12
                            ? suspended[source] / water[source] : 0.0;
                        double sediment_out = 0.0;
                        std::array<double, 4> edge_sediment{};
                        for (std::size_t direction = 0; direction < kEdges.size(); ++direction)
                        {
                            const Edge edge = edge_for(direction);
                            const double volume = flux[source * 4 + direction] * dt;
                            const double mass = concentration * volume;
                            edge_sediment[direction] = mass;
                            sediment_out += mass;
                            const int nx = static_cast<int>(source % domain.width) + edge.dx;
                            const int ny = static_cast<int>(source / domain.width) + edge.dy;
                            if (nx >= 0 && ny >= 0 && nx < static_cast<int>(domain.width) &&
                                ny < static_cast<int>(domain.height))
                                next_water[static_cast<std::size_t>(ny) * domain.width +
                                    static_cast<std::uint32_t>(nx)] += volume / cell_area;
                            else
                                exported_water[source] += volume;
                        }
                        const double available_mass = suspended[source] * cell_area;
                        const double mass_scale = sediment_out > 0.0
                            ? std::min(1.0, available_mass / sediment_out) : 1.0;
                        double actual_sediment_out = 0.0;
                        for (std::size_t direction = 0; direction < kEdges.size(); ++direction)
                        {
                            const double mass = edge_sediment[direction] * mass_scale;
                            actual_sediment_out += mass;
                            const Edge edge = edge_for(direction);
                            const int nx = static_cast<int>(source % domain.width) + edge.dx;
                            const int ny = static_cast<int>(source / domain.width) + edge.dy;
                            if (nx >= 0 && ny >= 0 && nx < static_cast<int>(domain.width) &&
                                ny < static_cast<int>(domain.height))
                                next_suspended[static_cast<std::size_t>(ny) * domain.width +
                                    static_cast<std::uint32_t>(nx)] += mass / cell_area;
                            else
                            {
                                exported_sediment[source] += mass;
                                exported_sediment_total += mass;
                            }
                        }
                        next_water[source] -= discharge[source] * dt / cell_area;
                        next_suspended[source] -= actual_sediment_out / cell_area;
                    }
                    water.swap(next_water);
                    suspended.swap(next_suspended);

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        if (obstacles[i] >= 0.5f) continue;
                        const double capacity = capacity_coefficient * std::sin(slope[i]) * speed[i];
                        if (suspended[i] < capacity)
                        {
                            double demand = dissolution_rate * erodibility[i] *
                                (capacity - suspended[i]) * dt;
                            const double soil_mass = soil[i] * soil_density;
                            const double from_soil = std::min(soil_mass, demand);
                            soil[i] -= from_soil / soil_density;
                            suspended[i] += from_soil;
                            demand -= from_soil;
                            const double weathering = std::min({demand,
                                bedrock_weathering_rate * dt * bedrock_density *
                                    (1.0 - hardness[i]) * erodibility[i],
                                maximum_bedrock_weathering * bedrock_density});
                            bedrock[i] -= weathering / bedrock_density;
                            suspended[i] += weathering;
                            eroded_bedrock[i] += weathering;
                        }
                        else
                        {
                            const double deposition = std::min(suspended[i],
                                deposition_rate * (suspended[i] - capacity) * dt);
                            soil[i] += deposition / soil_density;
                            suspended[i] -= deposition;
                            deposited_soil[i] += deposition / soil_density;
                        }
                    }

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const double evaporation = std::min(water[i], evaporation_rate * dt);
                        water[i] -= evaporation;
                        evaporated_volume += evaporation * cell_area;
                        surface[i] = bedrock[i] + soil[i] + sand[i];
                    }
                    elapsed += dt;
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        if (!std::isfinite(bedrock[i]) || !std::isfinite(soil[i]) ||
                            !std::isfinite(sand[i]) || !std::isfinite(water[i]) ||
                            !std::isfinite(suspended[i]) || soil[i] < -1.0e-10 ||
                            sand[i] < -1.0e-10 || water[i] < -1.0e-10 ||
                            suspended[i] < -1.0e-10)
                            throw std::runtime_error("hydraulic iteration produced an invalid state");
                    }
                }

                exported_water_total = std::accumulate(exported_water.begin(),
                    exported_water.end(), 0.0);
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (!std::isfinite(bedrock[i]) || !std::isfinite(soil[i]) || !std::isfinite(sand[i]) ||
                        !std::isfinite(water[i]) || !std::isfinite(suspended[i]) ||
                        soil[i] < -1.0e-10 || sand[i] < -1.0e-10 ||
                        water[i] < -1.0e-10 || suspended[i] < -1.0e-10)
                        throw std::runtime_error("hydraulic solve produced a non-finite or negative state");
                    water[i] = std::max(0.0, water[i]);
                    soil[i] = std::max(0.0, soil[i]);
                    sand[i] = std::max(0.0, sand[i]);
                    suspended[i] = std::max(0.0, suspended[i]);
                }

                std::vector<float> bedrock_out(count), soil_out(count), sand_out(count), water_out(count),
                    suspended_out(count), surface_out(count), speed_out(count), discharge_out(count),
                    eroded_out(count), deposited_out(count), exported_water_out(count),
                    exported_sediment_out(count), water_residual_out(count),
                    water_relative_residual_out(count), solid_residual_out(count),
                    solid_relative_residual_out(count);
                for (std::size_t i = 0; i < count; ++i)
                {
                    bedrock_out[i] = static_cast<float>(bedrock[i]);
                    soil_out[i] = static_cast<float>(soil[i]);
                    sand_out[i] = static_cast<float>(sand[i]);
                    water_out[i] = static_cast<float>(water[i]);
                    suspended_out[i] = static_cast<float>(suspended[i]);
                    surface_out[i] = bedrock_out[i] + soil_out[i] + sand_out[i];
                    speed_out[i] = static_cast<float>(speed[i]);
                    discharge_out[i] = static_cast<float>(discharge[i]);
                    eroded_out[i] = static_cast<float>(std::max(0.0,
                        static_cast<double>(bedrock_input[i]) - bedrock_out[i]) *
                        bedrock_density);
                    deposited_out[i] = static_cast<float>(deposited_soil[i]);
                    exported_water_out[i] = static_cast<float>(exported_water[i]);
                    exported_sediment_out[i] = static_cast<float>(exported_sediment[i]);
                }
                double final_water_volume = 0.0;
                double final_solid_mass = 0.0;
                bedrock_eroded_mass = 0.0;
                exported_water_total = 0.0;
                exported_sediment_total = 0.0;
                for (std::size_t i = 0; i < count; ++i)
                {
                    final_water_volume += static_cast<double>(water_out[i]) * cell_area;
                    final_solid_mass += (static_cast<double>(soil_out[i]) * soil_density +
                        static_cast<double>(sand_out[i]) * sand_density +
                        suspended_out[i]) * cell_area;
                    bedrock_eroded_mass += static_cast<double>(eroded_out[i]) * cell_area;
                    exported_water_total += exported_water_out[i];
                    exported_sediment_total += exported_sediment_out[i];
                }
                const double water_residual = initial_water_volume + rain_volume -
                    evaporated_volume - exported_water_total - final_water_volume;
                const double solid_residual = initial_solid_mass + bedrock_eroded_mass -
                    exported_sediment_total - final_solid_mass;
                const double water_scale = std::max(1.0e-12,
                    initial_water_volume + rain_volume);
                const double solid_scale = std::max(1.0e-12,
                    initial_solid_mass + bedrock_eroded_mass);
                const double water_relative_residual = water_residual / water_scale;
                const double solid_relative_residual = solid_residual / solid_scale;
                constexpr double kPublishedBudgetRelativeTolerance = 2.0e-6;
                if (std::abs(water_relative_residual) > kPublishedBudgetRelativeTolerance ||
                    std::abs(solid_relative_residual) > kPublishedBudgetRelativeTolerance)
                    throw std::runtime_error("published hydraulic state exceeded its relative water or solid budget tolerance");
                std::fill(water_residual_out.begin(), water_residual_out.end(),
                    static_cast<float>(water_residual));
                std::fill(water_relative_residual_out.begin(), water_relative_residual_out.end(),
                    static_cast<float>(water_relative_residual));
                std::fill(solid_residual_out.begin(), solid_residual_out.end(),
                    static_cast<float>(solid_residual));
                std::fill(solid_relative_residual_out.begin(), solid_relative_residual_out.end(),
                    static_cast<float>(solid_relative_residual));
                std::string error;
                auto output_state = LayeredHeightfield2D::Create(domain,
                    bedrock_out, soil_out, sand_out, water_out, suspended_out,
                    context.maximum_samples, error);
                if (!output_state) throw std::runtime_error(error);
                auto surface_field = MakeField(context, std::move(surface_out));
                return OperatorOutputs{
                    {"state", std::move(output_state)},
                    {"height", surface_field},
                    {"surface_height_m", std::move(surface_field)},
                    {"bedrock_elevation_m", MakeField(context, std::move(bedrock_out))},
                    {"soil_thickness_m", MakeField(context, std::move(soil_out))},
                    {"sand_thickness_m", MakeField(context, std::move(sand_out))},
                    {"water_depth_m", MakeField(context, std::move(water_out))},
                    {"suspended_sediment_kg_per_m2", MakeField(context, std::move(suspended_out))},
                    {"flow_speed_m_per_s", MakeField(context, std::move(speed_out))},
                    {"discharge_m3_per_s", MakeField(context, std::move(discharge_out))},
                    {"bedrock_eroded_kg_per_m2", MakeField(context, std::move(eroded_out))},
                    {"soil_deposited_m", MakeField(context, std::move(deposited_out))},
                    {"water_exported_m3", MakeField(context, std::move(exported_water_out))},
                    {"sediment_exported_kg", MakeField(context, std::move(exported_sediment_out))},
                    {"substep_count", MakeField(context,
                        std::vector<float>(count, static_cast<float>(substep)))},
                    {"simulated_time_s", MakeField(context,
                        std::vector<float>(count, static_cast<float>(elapsed)))},
                    {"water_budget_residual_m3", MakeField(context, std::move(water_residual_out))},
                    {"water_budget_relative_residual", MakeField(context, std::move(water_relative_residual_out))},
                    {"solid_budget_residual_kg", MakeField(context, std::move(solid_residual_out))},
                    {"solid_budget_relative_residual", MakeField(context, std::move(solid_relative_residual_out))}};
            }}, diagnostic);
    }
}
