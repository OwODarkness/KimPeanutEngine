#include "settings_store.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kpengine::settings
{
    namespace
    {
        using Json = nlohmann::json;
        using reflection::ReflectionValue;
        constexpr uint32_t kSettingsFormatVersion = 1;

        struct ReadFileResult
        {
            bool exists = false;
            bool success = false;
            std::string contents;
            std::string diagnostic;
        };

        ReadFileResult ReadFile(const std::filesystem::path &path)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
            {
                return {false, false, {}, error.message()};
            }
            if (!exists)
            {
                return {false, true, {}, {}};
            }

            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return {true, false, {}, "could not open settings file for reading"};
            }
            std::string contents{std::istreambuf_iterator<char>{input},
                                 std::istreambuf_iterator<char>{}};
            if (input.bad())
            {
                return {true, false, {}, "failed while reading settings file"};
            }
            return {true, true, std::move(contents), {}};
        }

        std::optional<ReflectionValue> DecodeValue(
            const Json &json,
            std::optional<reflection::ReflectionValueType> expected_type = std::nullopt)
        {
            if (json.is_boolean())
            {
                return ReflectionValue{json.get<bool>()};
            }
            if (json.is_number_unsigned())
            {
                const uint64_t value = json.get<uint64_t>();
                if (expected_type == reflection::ReflectionValueType::SignedInteger)
                {
                    if (value <= static_cast<uint64_t>(
                                     (std::numeric_limits<int64_t>::max)()))
                    {
                        return ReflectionValue{static_cast<int64_t>(value)};
                    }
                    return std::nullopt;
                }
                if (!expected_type ||
                    *expected_type == reflection::ReflectionValueType::UnsignedInteger)
                {
                    if (!expected_type &&
                        value <= static_cast<uint64_t>(
                                     (std::numeric_limits<int64_t>::max)()))
                    {
                        return ReflectionValue{static_cast<int64_t>(value)};
                    }
                    return ReflectionValue{value};
                }
                if (*expected_type == reflection::ReflectionValueType::FloatingPoint)
                {
                    const double converted = static_cast<double>(value);
                    if (std::isfinite(converted) &&
                        static_cast<long double>(converted) ==
                            static_cast<long double>(value))
                    {
                        return ReflectionValue{converted};
                    }
                }
                return std::nullopt;
            }
            if (json.is_number_integer())
            {
                const int64_t value = json.get<int64_t>();
                if (expected_type == reflection::ReflectionValueType::UnsignedInteger)
                {
                    if (value >= 0)
                    {
                        return ReflectionValue{static_cast<uint64_t>(value)};
                    }
                    return std::nullopt;
                }
                if (!expected_type ||
                    *expected_type == reflection::ReflectionValueType::SignedInteger)
                {
                    return ReflectionValue{value};
                }
                if (*expected_type == reflection::ReflectionValueType::FloatingPoint)
                {
                    const double converted = static_cast<double>(value);
                    if (std::isfinite(converted) &&
                        static_cast<long double>(converted) ==
                            static_cast<long double>(value))
                    {
                        return ReflectionValue{converted};
                    }
                }
                return std::nullopt;
            }
            if (json.is_number_float())
            {
                const double value = json.get<double>();
                if (std::isfinite(value))
                {
                    return ReflectionValue{value};
                }
                return std::nullopt;
            }
            if (json.is_string())
            {
                return ReflectionValue{json.get<std::string>()};
            }
            return std::nullopt;
        }

        Json EncodeValue(const ReflectionValue &value)
        {
            if (const bool *typed = value.TryGet<bool>())
            {
                return *typed;
            }
            if (const int64_t *typed = value.TryGet<int64_t>())
            {
                return *typed;
            }
            if (const uint64_t *typed = value.TryGet<uint64_t>())
            {
                return *typed;
            }
            if (const double *typed = value.TryGet<double>())
            {
                if (!std::isfinite(*typed))
                {
                    throw std::invalid_argument("non-finite setting values cannot be saved");
                }
                return *typed;
            }
            if (const std::string *typed = value.TryGet<std::string>())
            {
                return *typed;
            }
            throw std::invalid_argument("unsupported setting value cannot be saved");
        }

        SettingValues MakeDefaults(const SettingSectionDefinition &section)
        {
            SettingValues values;
            for (const auto &property : section.properties)
            {
                values.emplace(property.key, property.default_value);
            }
            return values;
        }

        bool IsScopeAllowed(const SettingSectionDefinition &section,
                            SettingSaveScope scope) noexcept
        {
            for (const SettingSaveScope allowed : section.allowed_save_scopes)
            {
                if (allowed == scope)
                {
                    return true;
                }
            }
            return false;
        }

        std::string ErrorMessage(const std::error_code &error)
        {
            return error ? error.message() : "unknown filesystem error";
        }

        bool AtomicReplace(const std::filesystem::path &temporary,
                           const std::filesystem::path &destination,
                           std::string &diagnostic)
        {
#if defined(_WIN32)
            const DWORD attributes = GetFileAttributesW(destination.c_str());
            const bool destination_exists = attributes != INVALID_FILE_ATTRIBUTES;
            const BOOL replaced = destination_exists
                                      ? ReplaceFileW(destination.c_str(), temporary.c_str(),
                                                     nullptr, REPLACEFILE_WRITE_THROUGH,
                                                     nullptr, nullptr)
                                      : MoveFileExW(temporary.c_str(), destination.c_str(), 0);
            if (replaced == 0)
            {
                diagnostic = std::system_category().message(
                    static_cast<int>(GetLastError()));
                return false;
            }
#else
            std::error_code error;
            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                diagnostic = error.message();
                return false;
            }
#endif
            return true;
        }

        struct TemporaryFileCleanup
        {
            std::filesystem::path path;
            ~TemporaryFileCleanup()
            {
                if (!path.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
            }
        };

        std::optional<uint32_t> ReadVersion(const Json &value)
        {
            if (value.is_number_unsigned())
            {
                const uint64_t version = value.get<uint64_t>();
                if (version > 0 && version <= (std::numeric_limits<uint32_t>::max)())
                {
                    return static_cast<uint32_t>(version);
                }
                return std::nullopt;
            }
            if (value.is_number_integer())
            {
                const int64_t version = value.get<int64_t>();
                if (version > 0 &&
                    static_cast<uint64_t>(version) <=
                        (std::numeric_limits<uint32_t>::max)())
                {
                    return static_cast<uint32_t>(version);
                }
            }
            return std::nullopt;
        }
    }

    struct SettingsStore::State
    {
        std::filesystem::path path;
        SettingSaveScope scope = SettingSaveScope::Project;
        Json document = Json::object();
        SettingsStoreSnapshot snapshot;
        std::string loaded_contents;
        std::map<std::string, std::vector<std::string>, std::less<>> migration_remove_keys;
        bool file_existed = false;
    };

    SettingsStore::SettingsStore(std::filesystem::path path, SettingSaveScope scope)
        : state_(std::make_unique<State>())
    {
        state_->path = std::move(path);
        state_->scope = scope;
        state_->snapshot.scope = scope;
    }

    SettingsStore::~SettingsStore() = default;
    SettingsStore::SettingsStore(SettingsStore &&) noexcept = default;
    SettingsStore &SettingsStore::operator=(SettingsStore &&) noexcept = default;

    SettingsStoreResult SettingsStore::Load(const SettingsRegistry &registry)
    {
        if (!state_)
        {
            return {SettingsStoreStatus::NotLoaded, "settings store has no state"};
        }
        if (!registry.IsSealed())
        {
            return {SettingsStoreStatus::RegistryNotSealed,
                    "settings schema catalog must be sealed before loading"};
        }
        if (state_->path.empty())
        {
            return {SettingsStoreStatus::IoError, "settings file path cannot be empty"};
        }

        const ReadFileResult file = ReadFile(state_->path);
        if (!file.success)
        {
            state_->snapshot = {};
            state_->snapshot.scope = state_->scope;
            state_->snapshot.loaded = true;
            state_->snapshot.diagnostics.push_back({{}, file.diagnostic});
            state_->snapshot.writable = false;
            return {SettingsStoreStatus::IoError, file.diagnostic};
        }

        state_->snapshot = {};
        state_->snapshot.scope = state_->scope;
        state_->snapshot.loaded = true;
        state_->snapshot.writable = true;
        state_->migration_remove_keys.clear();
        state_->loaded_contents = file.contents;
        state_->file_existed = file.exists;

        if (!file.exists)
        {
            state_->document = Json::object({
                {"format_version", kSettingsFormatVersion},
                {"sections", Json::object()},
            });
            return {SettingsStoreStatus::MissingFile, "settings file does not exist; defaults apply"};
        }

        Json document;
        try
        {
            document = Json::parse(file.contents);
        }
        catch (const Json::exception &exception)
        {
            state_->snapshot.writable = false;
            state_->snapshot.diagnostics.push_back({{}, exception.what()});
            return {SettingsStoreStatus::InvalidDocument,
                    std::string{"settings JSON is malformed: "} + exception.what()};
        }
        if (!document.is_object() || !document.contains("format_version") ||
            !document.contains("sections") || !document["sections"].is_object())
        {
            state_->snapshot.writable = false;
            state_->snapshot.diagnostics.push_back(
                {{}, "settings document must contain a version and section object"});
            return {SettingsStoreStatus::InvalidDocument,
                    "settings document must contain a version and section object"};
        }

        const std::optional<uint32_t> format_version = ReadVersion(document["format_version"]);
        if (!format_version)
        {
            state_->snapshot.writable = false;
            state_->snapshot.diagnostics.push_back({{}, "settings format version is invalid"});
            return {SettingsStoreStatus::InvalidDocument, "settings format version is invalid"};
        }
        if (*format_version != kSettingsFormatVersion)
        {
            const std::string diagnostic = "unsupported settings format version " +
                                           std::to_string(*format_version);
            state_->snapshot.writable = false;
            state_->snapshot.diagnostics.push_back({{}, diagnostic});
            return {SettingsStoreStatus::UnsupportedVersion, diagnostic};
        }

        state_->document = std::move(document);
        const auto sections = state_->document.find("sections");
        for (auto section_it = sections->begin(); section_it != sections->end(); ++section_it)
        {
            const std::optional<SettingSectionDefinition> schema =
                registry.FindSection(section_it.key());
            if (!schema)
            {
                continue;
            }

            const auto reject_section = [this, &section_it](std::string diagnostic)
            {
                state_->snapshot.diagnostics.push_back({section_it.key(), diagnostic});
                state_->snapshot.writable = false;
            };

            if (!IsScopeAllowed(*schema, state_->scope))
            {
                reject_section("settings section is not allowed in this save scope");
                continue;
            }
            if (!section_it.value().is_object() ||
                !section_it.value().contains("schema_version") ||
                !section_it.value().contains("values") ||
                !section_it.value()["values"].is_object())
            {
                reject_section("known settings section has an invalid structure");
                continue;
            }
            const std::optional<uint32_t> stored_version =
                ReadVersion(section_it.value()["schema_version"]);
            if (!stored_version)
            {
                reject_section("section schema version is invalid");
                continue;
            }
            if (*stored_version > schema->schema_version)
            {
                reject_section("section was written by a newer schema version");
                continue;
            }

            SettingValues decoded_values;
            bool values_valid = true;
            std::string value_diagnostic;
            for (auto value_it = section_it.value()["values"].begin();
                 value_it != section_it.value()["values"].end(); ++value_it)
            {
                const auto property = std::find_if(
                    schema->properties.begin(), schema->properties.end(),
                    [&value_it](const SettingPropertyDefinition &candidate)
                    {
                        return candidate.key == value_it.key();
                    });
                const std::optional<ReflectionValue> decoded = DecodeValue(
                    value_it.value(), property == schema->properties.end()
                                          ? std::nullopt
                                          : std::optional{property->value_type});
                if (!decoded)
                {
                    if (property == schema->properties.end())
                    {
                        continue;
                    }
                    values_valid = false;
                    value_diagnostic = "property '" + property->key +
                                       "' is not a supported scalar value";
                    break;
                }
                decoded_values.emplace(value_it.key(), *decoded);
            }
            if (!values_valid)
            {
                reject_section(std::move(value_diagnostic));
                continue;
            }

            if (*stored_version < schema->schema_version)
            {
                if (!schema->migrate)
                {
                    reject_section("section requires a migration that is not registered");
                    continue;
                }
                SettingMigrationResult migrated = schema->migrate(*stored_version, decoded_values);
                if (!migrated.migrated)
                {
                    reject_section(migrated.diagnostic.empty()
                                       ? "section migration failed"
                                       : std::move(migrated.diagnostic));
                    continue;
                }
                state_->migration_remove_keys[schema->key] = migrated.remove_keys;
                decoded_values = std::move(migrated.values);
            }

            SettingValues candidate = MakeDefaults(*schema);
            for (const auto &[key, value] : decoded_values)
            {
                const auto known = candidate.find(key);
                if (known == candidate.end())
                {
                    continue;
                }
                known->second = value;
            }
            if (const SettingsResult result = registry.ValidateValues(schema->key, candidate);
                !result)
            {
                reject_section(result.diagnostic);
                continue;
            }

            SettingValues known_overrides;
            for (const auto &property : schema->properties)
            {
                const auto value = decoded_values.find(property.key);
                if (value != decoded_values.end())
                {
                    known_overrides.emplace(property.key, value->second);
                }
            }
            if (!known_overrides.empty())
            {
                state_->snapshot.overrides.emplace(schema->key, std::move(known_overrides));
            }
        }
        return {};
    }

    SettingsStoreSnapshot SettingsStore::GetSnapshot() const
    {
        return state_ ? state_->snapshot : SettingsStoreSnapshot{};
    }

    SettingsStoreResult SettingsStore::Save(
        const SettingsRegistry &registry,
        const SettingsLayerOverrides &accepted_overrides)
    {
        if (!state_ || !state_->snapshot.loaded)
        {
            return {SettingsStoreStatus::NotLoaded,
                    "settings layer must be loaded before it can be saved"};
        }
        if (!registry.IsSealed())
        {
            return {SettingsStoreStatus::RegistryNotSealed,
                    "settings schema catalog must be sealed before saving"};
        }
        if (state_->path.empty())
        {
            return {SettingsStoreStatus::IoError, "settings file path cannot be empty"};
        }
        if (!state_->snapshot.writable)
        {
            return {SettingsStoreStatus::ReadOnly,
                    "settings file cannot be safely overwritten; reload or repair it first"};
        }

        for (const auto &[section_key, overrides] : accepted_overrides)
        {
            const std::optional<SettingSectionDefinition> schema =
                registry.FindSection(section_key);
            if (!schema)
            {
                return {SettingsStoreStatus::InvalidValues,
                        "cannot save unregistered settings section '" + section_key + "'"};
            }
            if (!IsScopeAllowed(*schema, state_->scope))
            {
                return {SettingsStoreStatus::ScopeNotAllowed,
                        "settings section '" + section_key +
                            "' is not allowed in this save scope"};
            }
            SettingValues candidate = MakeDefaults(*schema);
            for (const auto &[key, value] : overrides)
            {
                const auto property = candidate.find(key);
                if (property == candidate.end())
                {
                    return {SettingsStoreStatus::InvalidValues,
                            "cannot save unknown property '" + key + "' in section '" +
                                section_key + "'"};
                }
                property->second = value;
            }
            if (const SettingsResult result = registry.ValidateValues(section_key, candidate);
                !result)
            {
                return {SettingsStoreStatus::InvalidValues,
                        "section '" + section_key + "': " + result.diagnostic};
            }
        }

        const ReadFileResult current_file = ReadFile(state_->path);
        if (!current_file.success)
        {
            return {SettingsStoreStatus::IoError, current_file.diagnostic};
        }
        if (current_file.exists != state_->file_existed ||
            current_file.contents != state_->loaded_contents)
        {
            return {SettingsStoreStatus::DiskConflict,
                    "settings file changed since it was loaded; reload before saving"};
        }

        Json document = state_->document;
        Json &sections = document["sections"];
        for (const SettingSectionDefinition &schema : registry.GetSections())
        {
            const auto existing = sections.find(schema.key);
            const auto desired = accepted_overrides.find(schema.key);
            if (existing == sections.end() && desired == accepted_overrides.end())
            {
                continue;
            }
            if (existing == sections.end())
            {
                sections[schema.key] = Json::object();
            }
            Json &section = sections[schema.key];
            if (!section.is_object())
            {
                section = Json::object();
            }
            Json &values = section["values"];
            if (!values.is_object())
            {
                values = Json::object();
            }
            for (const SettingPropertyDefinition &property : schema.properties)
            {
                values.erase(property.key);
            }
            const auto migrated_keys = state_->migration_remove_keys.find(schema.key);
            if (migrated_keys != state_->migration_remove_keys.end())
            {
                for (const std::string &key : migrated_keys->second)
                {
                    values.erase(key);
                }
            }
            if (desired != accepted_overrides.end())
            {
                for (const auto &[key, value] : desired->second)
                {
                    values[key] = EncodeValue(value);
                }
                section["schema_version"] = schema.schema_version;
            }
            else if (!values.empty())
            {
                section["schema_version"] = schema.schema_version;
            }
            else if (section.size() == 2 && section.contains("schema_version") &&
                     section.contains("values"))
            {
                sections.erase(schema.key);
            }
        }

        document["format_version"] = kSettingsFormatVersion;
        std::string serialized;
        try
        {
            serialized = document.dump(2) + '\n';
        }
        catch (const Json::exception &exception)
        {
            return {SettingsStoreStatus::InvalidValues,
                    std::string{"settings values cannot be encoded as JSON: "} +
                        exception.what()};
        }
        std::error_code error;
        const std::filesystem::path parent = state_->path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                return {SettingsStoreStatus::IoError,
                        "could not create settings directory: " + ErrorMessage(error)};
            }
        }

        static std::atomic<uint64_t> next_temporary_id{0};
        std::filesystem::path temporary = state_->path;
        temporary += ".tmp-" + std::to_string(next_temporary_id.fetch_add(1));
        TemporaryFileCleanup cleanup{temporary};
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return {SettingsStoreStatus::IoError,
                        "could not open settings temporary file"};
            }
            output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
            output.flush();
            if (!output)
            {
                return {SettingsStoreStatus::IoError,
                        "could not flush settings temporary file"};
            }
            output.close();
            if (output.fail())
            {
                return {SettingsStoreStatus::IoError,
                        "could not close settings temporary file cleanly"};
            }
        }

        std::string replace_diagnostic;
        if (!AtomicReplace(temporary, state_->path, replace_diagnostic))
        {
            return {SettingsStoreStatus::IoError,
                    "could not atomically replace settings file: " + replace_diagnostic};
        }
        cleanup.path.clear();
        state_->document = std::move(document);
        state_->loaded_contents = serialized;
        state_->file_existed = true;
        state_->snapshot.overrides = accepted_overrides;
        state_->snapshot.diagnostics.clear();
        state_->snapshot.writable = true;
        state_->migration_remove_keys.clear();
        return {};
    }
}
