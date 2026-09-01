#include "level_loader.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/path.h"
#include "level.h"
#include "log/logger.h"
#include "utility.h"

namespace kpengine::asset
{
    namespace
    {
        using json = nlohmann::json;
        constexpr int kLevelVersion = 1;
        constexpr float kHalfPi = 1.57079632679489661923f;

        bool HasOnlyFields(const json &object, std::initializer_list<const char *> allowed)
        {
            if (!object.is_object())
            {
                return false;
            }
            for (const auto &[name, value] : object.items())
            {
                (void)value;
                bool found = false;
                for (const char *allowed_name : allowed)
                {
                    if (name == allowed_name)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
            return true;
        }

        class LevelParser
        {
        public:
            LevelParser(const std::string &path, AssetRegisterInfo &info)
                : path_(path), info_(info)
            {
            }

            bool Parse(const json &source)
            {
                if (!source.is_object() ||
                    !HasOnlyFields(source, {"version", "environment", "objects"}) ||
                    !source.contains("version") || !source["version"].is_number_integer() ||
                    !source.contains("objects") || !source["objects"].is_array())
                {
                    return Fail("root", "expected version and objects with no unknown fields");
                }

                const int version = source["version"].get<int>();
                if (version != kLevelVersion)
                {
                    return Fail("version", "unsupported version");
                }

                auto resource = std::make_shared<LevelResource>();
                resource->version = version;

                if (source.contains("environment") &&
                    !ParseEnvironment(source["environment"], resource->environment))
                {
                    return false;
                }

                std::unordered_set<std::string> object_ids;
                resource->objects.reserve(source["objects"].size());
                for (std::size_t index = 0; index < source["objects"].size(); ++index)
                {
                    const json &object = source["objects"][index];
                    const std::string location = "objects[" + std::to_string(index) + "]";
                    std::string id;
                    std::string name;
                    std::string kind;
                    if (!ParseRequiredString(object, "id", location, id) || id.empty())
                    {
                        return Fail(location + ".id", "must be a non-empty string");
                    }
                    if (!ParseOptionalString(object, "name", location, name) ||
                        !ParseRequiredString(object, "kind", location, kind))
                    {
                        return false;
                    }
                    if (!object_ids.insert(id).second)
                    {
                        return Fail(location + ".id", "duplicate object ID");
                    }

                    LevelObject parsed_object;
                    if (kind == "static_mesh")
                    {
                        LevelStaticMeshRecord record;
                        record.id = std::move(id);
                        record.name = std::move(name);
                        if (!ValidateOnlyFields(object, {"id", "name", "kind", "transform", "model",
                                                          "material", "visible", "casts_shadow", "lod_bias"}, location) ||
                            !ParseTransform(object, "transform", location, record.transform) ||
                            !ParseReference(object, "model", AssetType::KPAT_Model, location, record.model) ||
                            !ParseReference(object, "material", AssetType::KPAT_Material, location, record.material) ||
                            !ParseOptionalBool(object, "visible", location, record.visible) ||
                            !ParseOptionalBool(object, "casts_shadow", location, record.casts_shadow) ||
                            !ParseOptionalInt(object, "lod_bias", location, record.lod_bias))
                        {
                            return false;
                        }
                        if (record.lod_bias < 0)
                        {
                            return Fail(location + ".lod_bias", "must be finite and non-negative");
                        }
                        parsed_object = std::move(record);
                    }
                    else if (kind == "directional_light")
                    {
                        LevelDirectionalLightRecord record;
                        record.id = std::move(id);
                        record.name = std::move(name);
                        if (!ValidateOnlyFields(object, {"id", "name", "kind", "direction", "color",
                                                          "intensity", "enabled", "casts_shadow"}, location) ||
                            !ParseRequiredVector3(object, "direction", location, record.direction) ||
                            !ParseRequiredVector3(object, "color", location, record.color) ||
                            !ParseRequiredFloat(object, "intensity", location, record.intensity) ||
                            !ParseOptionalBool(object, "enabled", location, record.enabled) ||
                            !ParseOptionalBool(object, "casts_shadow", location, record.casts_shadow) ||
                            !ValidateNonZero(record.direction, location + ".direction") ||
                            !ValidateNonNegative(record.color, location + ".color") ||
                            !ValidateNonNegative(record.intensity, location + ".intensity"))
                        {
                            return false;
                        }
                        parsed_object = std::move(record);
                    }
                    else if (kind == "point_light")
                    {
                        LevelPointLightRecord record;
                        record.id = std::move(id);
                        record.name = std::move(name);
                        if (!ValidateOnlyFields(object, {"id", "name", "kind", "position", "color",
                                                          "intensity", "range", "enabled", "casts_shadow"}, location) ||
                            !ParseRequiredVector3(object, "position", location, record.position) ||
                            !ParseRequiredVector3(object, "color", location, record.color) ||
                            !ParseRequiredFloat(object, "intensity", location, record.intensity) ||
                            !ParseRequiredFloat(object, "range", location, record.range) ||
                            !ParseOptionalBool(object, "enabled", location, record.enabled) ||
                            !ParseOptionalBool(object, "casts_shadow", location, record.casts_shadow) ||
                            !ValidateNonNegative(record.color, location + ".color") ||
                            !ValidateNonNegative(record.intensity, location + ".intensity") ||
                            !ValidatePositive(record.range, location + ".range"))
                        {
                            return false;
                        }
                        parsed_object = std::move(record);
                    }
                    else if (kind == "spot_light")
                    {
                        LevelSpotLightRecord record;
                        record.id = std::move(id);
                        record.name = std::move(name);
                        if (!ValidateOnlyFields(object, {"id", "name", "kind", "position", "direction", "color",
                                                          "intensity", "range", "inner_cone_radians",
                                                          "outer_cone_radians", "enabled", "casts_shadow"}, location) ||
                            !ParseRequiredVector3(object, "position", location, record.position) ||
                            !ParseRequiredVector3(object, "direction", location, record.direction) ||
                            !ParseRequiredVector3(object, "color", location, record.color) ||
                            !ParseRequiredFloat(object, "intensity", location, record.intensity) ||
                            !ParseRequiredFloat(object, "range", location, record.range) ||
                            !ParseRequiredFloat(object, "inner_cone_radians", location, record.inner_cone_radians) ||
                            !ParseRequiredFloat(object, "outer_cone_radians", location, record.outer_cone_radians) ||
                            !ParseOptionalBool(object, "enabled", location, record.enabled) ||
                            !ParseOptionalBool(object, "casts_shadow", location, record.casts_shadow) ||
                            !ValidateNonZero(record.direction, location + ".direction") ||
                            !ValidateNonNegative(record.color, location + ".color") ||
                            !ValidateNonNegative(record.intensity, location + ".intensity") ||
                            !ValidatePositive(record.range, location + ".range") ||
                            !ValidateCones(record.inner_cone_radians, record.outer_cone_radians, location))
                        {
                            return false;
                        }
                        parsed_object = std::move(record);
                    }
                    else if (kind == "camera")
                    {
                        LevelCameraRecord record;
                        record.id = std::move(id);
                        record.name = std::move(name);
                        std::string projection;
                        if (!ValidateOnlyFields(object, {"id", "name", "kind", "transform", "projection",
                                                          "near_plane", "far_plane", "field_of_view_degrees",
                                                          "orthographic_height", "enabled", "priority"}, location) ||
                            !ParseTransform(object, "transform", location, record.transform) ||
                            !ParseRequiredString(object, "projection", location, projection) ||
                            !ParseRequiredFloat(object, "near_plane", location, record.near_plane) ||
                            !ParseRequiredFloat(object, "far_plane", location, record.far_plane) ||
                            !ParseOptionalFloat(object, "field_of_view_degrees", location,
                                                record.field_of_view_degrees) ||
                            !ParseOptionalFloat(object, "orthographic_height", location,
                                                record.orthographic_height) ||
                            !ParseOptionalBool(object, "enabled", location, record.enabled) ||
                            !ParseOptionalInt(object, "priority", location, record.priority))
                        {
                            return false;
                        }
                        if (projection == "perspective")
                        {
                            record.projection = LevelProjection::Perspective;
                        }
                        else if (projection == "orthographic")
                        {
                            record.projection = LevelProjection::Orthographic;
                        }
                        else
                        {
                            return Fail(location + ".projection", "must be perspective or orthographic");
                        }
                        if (record.field_of_view_degrees < 1.0f ||
                            record.field_of_view_degrees > 179.0f)
                        {
                            return Fail(location + ".field_of_view_degrees", "must be in [1, 179]");
                        }
                        if (!ValidatePositive(record.near_plane, location + ".near_plane") ||
                            !ValidatePositive(record.far_plane, location + ".far_plane") ||
                            record.far_plane <= record.near_plane ||
                            !ValidatePositive(record.orthographic_height, location + ".orthographic_height"))
                        {
                            return false;
                        }
                        parsed_object = std::move(record);
                    }
                    else
                    {
                        return Fail(location + ".kind", "unknown object kind");
                    }
                    resource->objects.push_back(std::move(parsed_object));
                }

                info_.type = AssetType::KPAT_Level;
                info_.name = ExtractNameFromPath(path_);
                info_.path = path_;
                info_.resource = std::move(resource);
                return true;
            }

        private:
            bool Fail(const std::string &location, const std::string &reason) const
            {
                KP_LOG("LevelLoaderLog", LOG_LEVEL_ERROR, "%s: %s (%s)",
                       path_.c_str(), location.c_str(), reason.c_str());
                return false;
            }

            bool ParseRequiredString(const json &object, const char *name,
                                     const std::string &location, std::string &value) const
            {
                if (!object.is_object() || !object.contains(name) || !object[name].is_string())
                {
                    return Fail(location + "." + name, "must be a string");
                }
                value = object[name].get<std::string>();
                return true;
            }

            bool ValidateOnlyFields(const json &object, std::initializer_list<const char *> allowed,
                                    const std::string &location) const
            {
                return HasOnlyFields(object, allowed) ||
                       Fail(location, "contains an unknown field or is not an object");
            }

            bool ParseOptionalString(const json &object, const char *name,
                                     const std::string &location, std::string &value) const
            {
                if (!object.contains(name))
                {
                    return true;
                }
                if (!object[name].is_string())
                {
                    return Fail(location + "." + name, "must be a string");
                }
                value = object[name].get<std::string>();
                return true;
            }

            bool ParseRequiredFloat(const json &object, const char *name,
                                    const std::string &location, float &value) const
            {
                if (!object.contains(name) || !object[name].is_number())
                {
                    return Fail(location + "." + name, "must be a finite number");
                }
                try
                {
                    value = object[name].get<float>();
                }
                catch (const json::exception &)
                {
                    return Fail(location + "." + name, "must be a finite number");
                }
                if (!std::isfinite(value))
                {
                    return Fail(location + "." + name, "must be finite");
                }
                return true;
            }

            bool ParseOptionalFloat(const json &object, const char *name,
                                    const std::string &location, float &value) const
            {
                if (!object.contains(name))
                {
                    return true;
                }
                return ParseRequiredFloat(object, name, location, value);
            }

            bool ParseOptionalInt(const json &object, const char *name,
                                  const std::string &location, int &value) const
            {
                if (!object.contains(name))
                {
                    return true;
                }
                if (!object[name].is_number_integer())
                {
                    return Fail(location + "." + name, "must be an integer");
                }
                try
                {
                    value = object[name].get<int>();
                }
                catch (const json::exception &)
                {
                    return Fail(location + "." + name, "must be an integer");
                }
                return true;
            }

            bool ParseOptionalBool(const json &object, const char *name,
                                   const std::string &location, bool &value) const
            {
                if (!object.contains(name))
                {
                    return true;
                }
                if (!object[name].is_boolean())
                {
                    return Fail(location + "." + name, "must be boolean");
                }
                value = object[name].get<bool>();
                return true;
            }

            bool ParseRequiredVector3(const json &object, const char *name,
                                      const std::string &location, Vector3f &value) const
            {
                if (!object.contains(name) || !object[name].is_array() || object[name].size() != 3)
                {
                    return Fail(location + "." + name, "must be a three-number array");
                }
                for (std::size_t index = 0; index < 3; ++index)
                {
                    if (!object[name][index].is_number())
                    {
                        return Fail(location + "." + name, "must contain only finite numbers");
                    }
                    try
                    {
                        value[index] = object[name][index].get<float>();
                    }
                    catch (const json::exception &)
                    {
                        return Fail(location + "." + name, "must contain only finite numbers");
                    }
                    if (!std::isfinite(value[index]))
                    {
                        return Fail(location + "." + name, "must contain only finite numbers");
                    }
                }
                return true;
            }

            bool ParseTransform(const json &object, const char *name,
                                const std::string &location, LevelTransform &transform) const
            {
                if (!object.contains(name) || !object[name].is_object() ||
                    !HasOnlyFields(object[name], {"position", "rotation_degrees", "scale"}) ||
                    !ParseRequiredVector3(object[name], "position", location + "." + name,
                                          transform.position) ||
                    !ParseRequiredVector3(object[name], "rotation_degrees", location + "." + name,
                                          transform.rotation_degrees) ||
                    !ParseRequiredVector3(object[name], "scale", location + "." + name,
                                          transform.scale))
                {
                    return Fail(location + "." + name, "must contain position, rotation_degrees, and scale");
                }
                if (transform.scale.x_ == 0.0f || transform.scale.y_ == 0.0f ||
                    transform.scale.z_ == 0.0f)
                {
                    return Fail(location + "." + name + ".scale", "components must be non-zero");
                }
                return true;
            }

            bool ParseReference(const json &object, const char *name, AssetType expected_type,
                                const std::string &location, LevelAssetReference &reference)
            {
                if (!object.contains(name) || !object[name].is_string())
                {
                    return Fail(location + "." + name, "must be a non-empty asset path");
                }
                const std::string authored_path = object[name].get<std::string>();
                std::string normalized;
                if (!NormalizeReference(authored_path, expected_type, normalized,
                                        location + "." + name))
                {
                    return false;
                }

                reference.path = normalized;
                reference.expected_type = expected_type;
                const std::string key = CanonicalAssetPathKey(normalized);
                const auto existing = dependency_indices_.find(key);
                if (existing != dependency_indices_.end())
                {
                    if (info_.dependency_requests[existing->second].expected_type != expected_type)
                    {
                        return Fail(location + "." + name, "asset path has incompatible types");
                    }
                    reference.dependency_index = existing->second;
                    return true;
                }

                const uint32_t index = static_cast<uint32_t>(info_.dependency_requests.size());
                dependency_indices_.emplace(key, index);
                const std::string resolved_path =
                    (std::filesystem::path(GetAssetDirectory()) / std::filesystem::path(normalized)).generic_string();
                info_.dependency_requests.push_back({resolved_path, expected_type});
                reference.dependency_index = index;
                return true;
            }

            bool NormalizeReference(const std::string &authored_path, AssetType expected_type,
                                    std::string &normalized, const std::string &location) const
            {
                if (authored_path.empty() || authored_path.find('\0') != std::string::npos)
                {
                    return Fail(location, "asset path is empty or contains a NUL");
                }

                std::string portable = authored_path;
                std::replace(portable.begin(), portable.end(), '\\', '/');
                if (portable.empty() || portable.front() == '/' ||
                    (portable.size() >= 2 && std::isalpha(static_cast<unsigned char>(portable[0])) &&
                     portable[1] == ':'))
                {
                    return Fail(location, "asset path must be asset-root-relative");
                }

                std::vector<std::string> segments;
                std::size_t begin = 0;
                while (begin <= portable.size())
                {
                    const std::size_t end = portable.find('/', begin);
                    const std::string segment = portable.substr(
                        begin, end == std::string::npos ? std::string::npos : end - begin);
                    if (segment == "..")
                    {
                        if (segments.empty())
                        {
                            return Fail(location, "asset path escapes the asset root");
                        }
                        segments.pop_back();
                    }
                    else if (!segment.empty() && segment != ".")
                    {
                        if (segment.find(':') != std::string::npos)
                        {
                            return Fail(location, "asset path contains a drive-qualified segment");
                        }
                        segments.push_back(segment);
                    }
                    if (end == std::string::npos)
                    {
                        break;
                    }
                    begin = end + 1;
                }

                if (segments.empty())
                {
                    return Fail(location, "asset path is empty after normalization");
                }
                normalized = segments.front();
                for (std::size_t index = 1; index < segments.size(); ++index)
                {
                    normalized += "/" + segments[index];
                }

                const std::string extension = GetFileExtension(normalized);
                if (ExtractAssetType(extension) != expected_type ||
                    (expected_type == AssetType::KPAT_Texture && extension != "hdr"))
                {
                    return Fail(location, "asset extension does not match the required type");
                }
                return true;
            }

            bool ParseEnvironment(const json &source,
                                   std::optional<LevelEnvironmentRecord> &environment)
            {
                const std::string location = "environment";
                if (!source.is_object() ||
                    !HasOnlyFields(source, {"texture", "ibl_intensity"}) ||
                    !source.contains("texture") || !source.contains("ibl_intensity"))
                {
                    return Fail(location, "must contain texture and ibl_intensity with no unknown fields");
                }
                LevelEnvironmentRecord record;
                if (!ParseReference(source, "texture", AssetType::KPAT_Texture, location, record.texture) ||
                    !ParseRequiredFloat(source, "ibl_intensity", location, record.ibl_intensity) ||
                    !ValidateNonNegative(record.ibl_intensity, location + ".ibl_intensity"))
                {
                    return false;
                }
                environment = std::move(record);
                return true;
            }

            bool ValidateNonZero(const Vector3f &value, const std::string &location) const
            {
                const double length_squared = static_cast<double>(value.x_) * value.x_ +
                                               static_cast<double>(value.y_) * value.y_ +
                                               static_cast<double>(value.z_) * value.z_;
                return length_squared > 0.0 || Fail(location, "must not be zero-length");
            }

            bool ValidateNonNegative(const Vector3f &value, const std::string &location) const
            {
                return (value.x_ >= 0.0f && value.y_ >= 0.0f && value.z_ >= 0.0f) ||
                       Fail(location, "components must be non-negative");
            }

            bool ValidateNonNegative(float value, const std::string &location) const
            {
                return (std::isfinite(value) && value >= 0.0f) ||
                       Fail(location, "must be finite and non-negative");
            }

            bool ValidatePositive(float value, const std::string &location) const
            {
                return (std::isfinite(value) && value > 0.0f) ||
                       Fail(location, "must be finite and positive");
            }

            bool ValidateCones(float inner, float outer, const std::string &location) const
            {
                return (inner >= 0.0f && inner <= outer && outer < kHalfPi) ||
                       Fail(location, "cones must satisfy 0 <= inner <= outer < pi/2");
            }

            const std::string &path_;
            AssetRegisterInfo &info_;
            std::unordered_map<std::string, uint32_t> dependency_indices_;
        };
    }

    bool LevelLoader::Load(const std::string &path, AssetRegisterInfo &info)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            KP_LOG("LevelLoaderLog", LOG_LEVEL_ERROR, "Failed to open %s", path.c_str());
            return false;
        }

        try
        {
            json source;
            file >> source;
            AssetRegisterInfo parsed_info{};
            LevelParser parser(path, parsed_info);
            if (!parser.Parse(source))
            {
                return false;
            }
            info = std::move(parsed_info);
            return true;
        }
        catch (const json::exception &exception)
        {
            KP_LOG("LevelLoaderLog", LOG_LEVEL_ERROR, "Failed to parse %s: %s",
                   path.c_str(), exception.what());
            return false;
        }
    }
}
