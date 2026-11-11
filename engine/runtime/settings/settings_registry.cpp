#include "settings_registry.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace kpengine::settings
{
    namespace
    {
        constexpr std::size_t kMaximumQueuedSettingsRequests = 64;
        constexpr std::size_t kMaximumRetainedSettingsResults = 128;

        SettingsResult Invalid(std::string diagnostic)
        {
            return {SettingsResultStatus::InvalidDefinition, std::move(diagnostic)};
        }

        bool IsCanonicalKey(std::string_view key, bool allow_dots) noexcept
        {
            if (key.empty() || key.front() == '.' || key.back() == '.')
            {
                return false;
            }

            bool previous_was_dot = false;
            bool contains_dot = false;
            for (const char character : key)
            {
                const bool is_dot = character == '.';
                const bool is_valid_character =
                    (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') || character == '_';
                if (is_dot)
                {
                    if (!allow_dots || previous_was_dot)
                    {
                        return false;
                    }
                    contains_dot = true;
                }
                else if (!is_valid_character)
                {
                    return false;
                }
                previous_was_dot = is_dot;
            }
            return !allow_dots || contains_dot;
        }

        bool IsValidValueType(reflection::ReflectionValueType type) noexcept
        {
            switch (type)
            {
            case reflection::ReflectionValueType::Bool:
            case reflection::ReflectionValueType::SignedInteger:
            case reflection::ReflectionValueType::UnsignedInteger:
            case reflection::ReflectionValueType::FloatingPoint:
            case reflection::ReflectionValueType::String:
                return true;
            }
            return false;
        }

        bool IsValidWidgetSemantic(reflection::ReflectionWidgetSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case reflection::ReflectionWidgetSemantic::Default:
            case reflection::ReflectionWidgetSemantic::Position:
            case reflection::ReflectionWidgetSemantic::RotationDegrees:
            case reflection::ReflectionWidgetSemantic::Scale:
            case reflection::ReflectionWidgetSemantic::Color:
            case reflection::ReflectionWidgetSemantic::Distance:
            case reflection::ReflectionWidgetSemantic::AngleRadians:
            case reflection::ReflectionWidgetSemantic::Enum:
                return true;
            }
            return false;
        }

        bool IsNumericType(reflection::ReflectionValueType type) noexcept
        {
            return type == reflection::ReflectionValueType::SignedInteger ||
                   type == reflection::ReflectionValueType::UnsignedInteger ||
                   type == reflection::ReflectionValueType::FloatingPoint;
        }

        bool IsFiniteNumericValue(const reflection::ReflectionValue &value) noexcept
        {
            if (const double *number = value.TryGet<double>())
            {
                return std::isfinite(*number);
            }
            return true;
        }

        long double NumericValue(const reflection::ReflectionValue &value) noexcept
        {
            if (const int64_t *number = value.TryGet<int64_t>())
            {
                return static_cast<long double>(*number);
            }
            if (const uint64_t *number = value.TryGet<uint64_t>())
            {
                return static_cast<long double>(*number);
            }
            if (const double *number = value.TryGet<double>())
            {
                return static_cast<long double>(*number);
            }
            return 0.0L;
        }

        SettingsResult ValidateMetadata(const SettingPropertyDefinition &property)
        {
            using reflection::ReflectionValueType;
            const auto &metadata = property.metadata;
            const bool numeric = IsNumericType(property.value_type);

            if (!IsValidWidgetSemantic(metadata.semantic))
            {
                return Invalid("property has an unsupported widget semantic");
            }
            if (!numeric && (metadata.minimum || metadata.maximum || metadata.step))
            {
                return Invalid("numeric metadata requires a numeric property");
            }
            if ((metadata.minimum && !std::isfinite(*metadata.minimum)) ||
                (metadata.maximum && !std::isfinite(*metadata.maximum)))
            {
                return Invalid("metadata bounds must be finite");
            }
            if (metadata.step && (!std::isfinite(*metadata.step) || *metadata.step <= 0.0))
            {
                return Invalid("metadata step must be finite and positive");
            }
            if (metadata.minimum && metadata.maximum && *metadata.minimum > *metadata.maximum)
            {
                return Invalid("metadata minimum cannot exceed maximum");
            }
            if (metadata.semantic != reflection::ReflectionWidgetSemantic::Default &&
                metadata.semantic != reflection::ReflectionWidgetSemantic::Enum && !numeric)
            {
                return Invalid("numeric widget semantics require a numeric property");
            }
            if (metadata.semantic == reflection::ReflectionWidgetSemantic::Enum &&
                property.value_type != ReflectionValueType::SignedInteger &&
                property.value_type != ReflectionValueType::UnsignedInteger)
            {
                return Invalid("enum widget semantics require an integer property");
            }
            if (!metadata.enum_options.empty() &&
                metadata.semantic != reflection::ReflectionWidgetSemantic::Enum)
            {
                return Invalid("enum options require enum widget semantics");
            }
            if (metadata.semantic == reflection::ReflectionWidgetSemantic::Enum &&
                metadata.enum_options.empty())
            {
                return Invalid("enum widget semantics require options");
            }
            for (std::size_t index = 0; index < metadata.enum_options.size(); ++index)
            {
                const auto &option = metadata.enum_options[index];
                if (option.label.empty())
                {
                    return Invalid("enum option labels cannot be empty");
                }
                for (std::size_t previous = 0; previous < index; ++previous)
                {
                    const auto &other = metadata.enum_options[previous];
                    if (option.value == other.value || option.label == other.label)
                    {
                        return Invalid("enum option values and labels must be unique");
                    }
                }
            }
            return {};
        }

        SettingsResult ValidateProperty(const SettingPropertyDefinition &property)
        {
            if (!IsCanonicalKey(property.key, false))
            {
                return Invalid("property key must use lowercase letters, digits, and underscores");
            }
            if (!IsValidValueType(property.value_type) ||
                property.default_value.GetType() != property.value_type)
            {
                return Invalid("property default type does not match its declared type");
            }
            if (!IsFiniteNumericValue(property.default_value))
            {
                return Invalid("floating-point defaults must be finite");
            }
            if (const SettingsResult result = ValidateMetadata(property); !result)
            {
                return result;
            }

            const long double default_value = NumericValue(property.default_value);
            if (property.metadata.minimum && default_value < *property.metadata.minimum)
            {
                return Invalid("property default is below its metadata minimum");
            }
            if (property.metadata.maximum && default_value > *property.metadata.maximum)
            {
                return Invalid("property default is above its metadata maximum");
            }
            if (property.metadata.semantic == reflection::ReflectionWidgetSemantic::Enum)
            {
                int64_t enum_default = 0;
                if (const int64_t *value = property.default_value.TryGet<int64_t>())
                {
                    enum_default = *value;
                }
                else if (const uint64_t *value = property.default_value.TryGet<uint64_t>();
                         value != nullptr && *value <=
                             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                {
                    enum_default = static_cast<int64_t>(*value);
                }
                else
                {
                    return Invalid("enum default is outside the supported option range");
                }
                bool found_default = false;
                for (const auto &option : property.metadata.enum_options)
                {
                    found_default = found_default || option.value == enum_default;
                }
                if (!found_default)
                {
                    return Invalid("enum default must match one of its options");
                }
            }
            return {};
        }

        SettingsResult ValidateSection(const SettingSectionDefinition &section)
        {
            if (!IsCanonicalKey(section.key, true))
            {
                return Invalid("section key must be a canonical dotted key");
            }
            if (section.label.empty() || section.category.empty())
            {
                return Invalid("section label and category cannot be empty");
            }
            if (section.schema_version == 0)
            {
                return Invalid("section schema version must be positive");
            }
            if (section.properties.empty())
            {
                return Invalid("section must define at least one property");
            }
            if (section.allowed_save_scopes.empty())
            {
                return Invalid("section must allow at least one persistent save scope");
            }

            std::set<SettingSaveScope> scopes;
            for (const SettingSaveScope scope : section.allowed_save_scopes)
            {
                if (scope != SettingSaveScope::Project && scope != SettingSaveScope::User)
                {
                    return Invalid("section declares an unsupported save scope");
                }
                if (!scopes.insert(scope).second)
                {
                    return Invalid("section save scopes must be unique");
                }
            }

            if (section.apply_policy != SettingApplyPolicy::Live &&
                section.apply_policy != SettingApplyPolicy::RestartRequired)
            {
                return Invalid("section declares an unsupported apply policy");
            }

            SettingValues defaults;
            for (const auto &property : section.properties)
            {
                if (const SettingsResult result = ValidateProperty(property); !result)
                {
                    return Invalid("property '" + property.key + "': " + result.diagnostic);
                }
                if (!defaults.emplace(property.key, property.default_value).second)
                {
                    return Invalid("section property keys must be unique");
                }
            }

            if (section.validate)
            {
                const SettingValidationResult validation = section.validate(defaults);
                if (!validation.valid)
                {
                    return Invalid(validation.diagnostic.empty()
                                       ? "section validator rejected its defaults"
                                       : validation.diagnostic);
                }
            }
            return {};
        }

        SettingsResult ValidateSectionValues(const SettingSectionDefinition &section,
                                             const SettingValues &values)
        {
            if (values.size() != section.properties.size())
            {
                return {SettingsResultStatus::InvalidValues,
                        "settings values must contain every declared property exactly once"};
            }

            for (const auto &property : section.properties)
            {
                const auto value = values.find(property.key);
                if (value == values.end())
                {
                    return {SettingsResultStatus::InvalidValues,
                            "settings value is missing property '" + property.key + "'"};
                }
                SettingPropertyDefinition candidate = property;
                candidate.default_value = value->second;
                if (const SettingsResult result = ValidateProperty(candidate); !result)
                {
                    return {SettingsResultStatus::InvalidValues,
                            "property '" + property.key + "': " + result.diagnostic};
                }
            }

            if (section.validate)
            {
                const SettingValidationResult validation = section.validate(values);
                if (!validation.valid)
                {
                    return {SettingsResultStatus::InvalidValues,
                            validation.diagnostic.empty()
                                ? "section validator rejected the values"
                                : validation.diagnostic};
                }
            }
            return {};
        }

        SettingValues SectionDefaults(const SettingSectionDefinition &section)
        {
            SettingValues defaults;
            for (const auto &property : section.properties)
            {
                defaults.emplace(property.key, property.default_value);
            }
            return defaults;
        }

        bool AllowsScope(const SettingSectionDefinition &section,
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

        bool IsPersistentScope(SettingSaveScope scope) noexcept
        {
            return scope == SettingSaveScope::Project || scope == SettingSaveScope::User;
        }

        std::vector<std::string> ChangedSections(const SettingsLayerOverrides &before,
                                                 const SettingsLayerOverrides &after)
        {
            std::set<std::string, std::less<>> changed;
            for (const auto &[section_key, values] : before)
            {
                const auto current = after.find(section_key);
                if (current == after.end() || current->second != values)
                {
                    changed.insert(section_key);
                }
            }
            for (const auto &[section_key, values] : after)
            {
                const auto previous = before.find(section_key);
                if (previous == before.end() || previous->second != values)
                {
                    changed.insert(section_key);
                }
            }
            return {changed.begin(), changed.end()};
        }
    }

    struct SettingsRegistry::CompletionControl
    {
        std::mutex mutex;
        SettingsRegistry *registry = nullptr;
    };

    SettingsRegistry::SettingsRegistry()
        : completion_control_(std::make_shared<CompletionControl>())
    {
        completion_control_->registry = this;
    }

    SettingsRegistry::~SettingsRegistry()
    {
        {
            std::lock_guard completion_lock(completion_control_->mutex);
            completion_control_->registry = nullptr;
        }
        std::lock_guard lock(mutex_);
        for (auto &[section_key, provider] : providers_)
        {
            (void)section_key;
            provider.registration->active.store(false, std::memory_order_release);
        }
        for (auto &[request_id, request] : requests_)
        {
            (void)request_id;
            if (request.result.status == SettingsEditStatus::Pending)
            {
                request.result.status = SettingsEditStatus::Cancelled;
                request.result.diagnostic = "settings registry shut down";
            }
        }
    }

    SettingsResult SettingsRegistry::RegisterSection(SettingSectionDefinition section)
    {
        if (const SettingsResult result = ValidateSection(section); !result)
        {
            return result;
        }

        std::lock_guard lock(mutex_);
        if (sealed_)
        {
            return {SettingsResultStatus::Sealed, "settings schema catalog is sealed"};
        }
        if (sections_.contains(section.key))
        {
            return {SettingsResultStatus::DuplicateSection,
                    "settings section '" + section.key + "' is already registered"};
        }
        sections_.emplace(section.key, std::move(section));
        return {};
    }

    SettingsResult SettingsRegistry::Seal()
    {
        std::lock_guard lock(mutex_);
        sealed_ = true;
        return {};
    }

    bool SettingsRegistry::IsSealed() const
    {
        std::lock_guard lock(mutex_);
        return sealed_;
    }

    bool SettingsRegistry::HasProvider(std::string_view section_key) const
    {
        std::lock_guard lock(mutex_);
        const auto provider = providers_.find(section_key);
        return provider != providers_.end() &&
               provider->second.registration->active.load(std::memory_order_acquire);
    }

    uint64_t SettingsRegistry::GetValuesRevision() const
    {
        std::lock_guard lock(mutex_);
        return values_revision_;
    }

    std::optional<SettingSectionDefinition> SettingsRegistry::FindSection(
        std::string_view key) const
    {
        std::lock_guard lock(mutex_);
        const auto found = sections_.find(key);
        if (found == sections_.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    std::vector<SettingSectionDefinition> SettingsRegistry::GetSections() const
    {
        std::lock_guard lock(mutex_);
        std::vector<SettingSectionDefinition> sections;
        sections.reserve(sections_.size());
        for (const auto &[key, section] : sections_)
        {
            (void)key;
            sections.push_back(section);
        }
        return sections;
    }

    SettingsResult SettingsRegistry::ValidateValues(std::string_view section_key,
                                                    const SettingValues &values) const
    {
        const auto section = FindSection(section_key);
        if (!section)
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings section '" + std::string{section_key} + "' is not registered"};
        }
        return ValidateSectionValues(*section, values);
    }

    SettingsResult SettingsRegistry::SetPersistentOverrides(
        SettingSaveScope scope, SettingsLayerOverrides overrides)
    {
        if (!IsPersistentScope(scope))
        {
            return {SettingsResultStatus::InvalidValues,
                    "persistent settings require project or user scope"};
        }

        std::lock_guard lock(mutex_);
        if (!sealed_)
        {
            return {SettingsResultStatus::Sealed,
                    "settings schema catalog must be sealed before resolving layers"};
        }
        for (const auto &[section_key, values] : overrides)
        {
            (void)values;
            const auto schema = sections_.find(section_key);
            if (schema == sections_.end())
            {
                return {SettingsResultStatus::InvalidValues,
                        "settings section '" + section_key + "' is not registered"};
            }
            if (!AllowsScope(schema->second, scope))
            {
                return {SettingsResultStatus::InvalidValues,
                        "settings section '" + section_key +
                            "' is not allowed in this save scope"};
            }
        }

        SettingsLayerOverrides &current = scope == SettingSaveScope::Project
                                              ? project_overrides_
                                              : user_overrides_;
        const std::vector<std::string> changed = ChangedSections(current, overrides);
        if (changed.empty())
        {
            return {};
        }
        current = std::move(overrides);
        MarkSectionRevisionsLocked(changed);
        return {};
    }

    void SettingsRegistry::MarkSectionRevisionsLocked(
        const std::vector<std::string> &section_keys)
    {
        if (section_keys.empty())
        {
            return;
        }
        ++values_revision_;
        for (const std::string &section_key : section_keys)
        {
            section_revisions_[section_key] = values_revision_;
        }
    }

    SettingsResult SettingsRegistry::SetSessionOverrides(SettingsLayerOverrides overrides)
    {
        std::lock_guard lock(mutex_);
        if (!sealed_)
        {
            return {SettingsResultStatus::Sealed,
                    "settings schema catalog must be sealed before resolving layers"};
        }
        for (const auto &[section_key, values] : overrides)
        {
            (void)values;
            if (!sections_.contains(section_key))
            {
                return {SettingsResultStatus::InvalidValues,
                        "settings section '" + section_key + "' is not registered"};
            }
        }
        const std::vector<std::string> changed = ChangedSections(session_overrides_, overrides);
        if (changed.empty())
        {
            return {};
        }
        session_overrides_ = std::move(overrides);
        MarkSectionRevisionsLocked(changed);
        return {};
    }

    SettingsResult SettingsRegistry::RemovePersistentOverride(
        SettingSaveScope scope, std::string_view section_key, std::string_view property_key)
    {
        if (!IsPersistentScope(scope))
        {
            return {SettingsResultStatus::InvalidValues,
                    "persistent settings require project or user scope"};
        }
        std::lock_guard lock(mutex_);
        if (!sealed_)
        {
            return {SettingsResultStatus::Sealed,
                    "settings schema catalog must be sealed before editing layers"};
        }
        const auto schema = sections_.find(section_key);
        if (schema == sections_.end())
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings section '" + std::string{section_key} + "' is not registered"};
        }
        if (!AllowsScope(schema->second, scope))
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings section is not allowed in this save scope"};
        }
        const auto property = std::find_if(
            schema->second.properties.begin(), schema->second.properties.end(),
            [property_key](const SettingPropertyDefinition &candidate)
            {
                return candidate.key == property_key;
            });
        if (property == schema->second.properties.end())
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings property '" + std::string{property_key} + "' is not registered"};
        }

        SettingsLayerOverrides &layer = scope == SettingSaveScope::Project
                                            ? project_overrides_
                                            : user_overrides_;
        const auto section = layer.find(section_key);
        if (section != layer.end() && section->second.erase(std::string{property_key}) != 0)
        {
            if (section->second.empty())
            {
                layer.erase(section);
            }
            MarkSectionRevisionsLocked({std::string{section_key}});
        }
        return {};
    }

    SettingsResult SettingsRegistry::RemoveSessionOverride(
        std::string_view section_key, std::string_view property_key)
    {
        std::lock_guard lock(mutex_);
        if (!sealed_)
        {
            return {SettingsResultStatus::Sealed,
                    "settings schema catalog must be sealed before editing layers"};
        }
        const auto schema = sections_.find(section_key);
        if (schema == sections_.end())
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings section '" + std::string{section_key} + "' is not registered"};
        }
        const auto property = std::find_if(
            schema->second.properties.begin(), schema->second.properties.end(),
            [property_key](const SettingPropertyDefinition &candidate)
            {
                return candidate.key == property_key;
            });
        if (property == schema->second.properties.end())
        {
            return {SettingsResultStatus::InvalidValues,
                    "settings property '" + std::string{property_key} + "' is not registered"};
        }
        const auto section = session_overrides_.find(section_key);
        if (section != session_overrides_.end() &&
            section->second.erase(std::string{property_key}) != 0)
        {
            if (section->second.empty())
            {
                session_overrides_.erase(section);
            }
            MarkSectionRevisionsLocked({std::string{section_key}});
        }
        return {};
    }

    SettingsLayerOverrides SettingsRegistry::GetPersistentOverrides(
        SettingSaveScope scope) const
    {
        std::lock_guard lock(mutex_);
        if (scope == SettingSaveScope::Project)
        {
            return project_overrides_;
        }
        if (scope == SettingSaveScope::User)
        {
            return user_overrides_;
        }
        return {};
    }

    SettingsLayerOverrides SettingsRegistry::GetSessionOverrides() const
    {
        std::lock_guard lock(mutex_);
        return session_overrides_;
    }

    SettingsResolvedSnapshot SettingsRegistry::GetResolvedSnapshot() const
    {
        std::vector<SettingSectionDefinition> schemas;
        SettingsLayerOverrides project_overrides;
        SettingsLayerOverrides user_overrides;
        SettingsLayerOverrides session_overrides;
        std::map<std::string, uint64_t, std::less<>> section_revisions;
        SettingsResolvedSnapshot snapshot;
        {
            std::lock_guard lock(mutex_);
            schemas.reserve(sections_.size());
            for (const auto &[key, schema] : sections_)
            {
                (void)key;
                schemas.push_back(schema);
            }
            project_overrides = project_overrides_;
            user_overrides = user_overrides_;
            session_overrides = session_overrides_;
            section_revisions = section_revisions_;
            snapshot.revision = values_revision_;
        }

        for (const SettingSectionDefinition &schema : schemas)
        {
            SettingValues resolved = SectionDefaults(schema);
            std::map<std::string, SettingValueSource, std::less<>> sources;
            for (const auto &property : schema.properties)
            {
                sources.emplace(property.key, SettingValueSource::CompiledDefault);
            }

            const auto apply_layer = [&](const SettingsLayerOverrides &layer,
                                         SettingValueSource source,
                                         std::optional<SettingSaveScope> persistent_scope)
            {
                const auto layer_section = layer.find(schema.key);
                if (layer_section == layer.end())
                {
                    return;
                }
                if (persistent_scope && !AllowsScope(schema, *persistent_scope))
                {
                    snapshot.diagnostics.push_back(
                        {schema.key, source, "section is not allowed in this save scope"});
                    return;
                }

                SettingValues candidate = resolved;
                bool known_properties = true;
                for (const auto &[property_key, value] : layer_section->second)
                {
                    const auto property = candidate.find(property_key);
                    if (property == candidate.end())
                    {
                        snapshot.diagnostics.push_back(
                            {schema.key, source,
                             "unknown property '" + property_key + "' in settings layer"});
                        known_properties = false;
                        break;
                    }
                    property->second = value;
                }
                if (!known_properties)
                {
                    return;
                }
                if (const SettingsResult result = ValidateSectionValues(schema, candidate);
                    !result)
                {
                    snapshot.diagnostics.push_back({schema.key, source, result.diagnostic});
                    return;
                }

                resolved = std::move(candidate);
                for (const auto &[property_key, value] : layer_section->second)
                {
                    (void)value;
                    sources[property_key] = source;
                }
            };

            apply_layer(project_overrides, SettingValueSource::Project,
                        SettingSaveScope::Project);
            apply_layer(user_overrides, SettingValueSource::User, SettingSaveScope::User);
            apply_layer(session_overrides, SettingValueSource::Session, std::nullopt);

            ResolvedSettingsSection resolved_section;
            resolved_section.key = schema.key;
            const auto revision = section_revisions.find(schema.key);
            resolved_section.revision =
                revision == section_revisions.end() ? 0 : revision->second;
            for (const auto &[property_key, value] : resolved)
            {
                resolved_section.properties.emplace(
                    property_key, ResolvedSettingValue{value, sources.at(property_key)});
            }
            snapshot.sections.push_back(std::move(resolved_section));
        }
        return snapshot;
    }

    SettingsProviderBindResult SettingsRegistry::BindProvider(
        std::string_view section_key, ISettingsProvider &provider)
    {
        std::lock_guard lock(mutex_);
        SweepInactiveProvidersLocked();
        if (!sealed_)
        {
            return {{SettingsResultStatus::Sealed,
                     "settings schema catalog must be sealed before binding providers"}, {}};
        }
        const auto schema = sections_.find(section_key);
        if (schema == sections_.end())
        {
            return {{SettingsResultStatus::InvalidValues,
                     "settings section '" + std::string{section_key} + "' is not registered"}, {}};
        }
        if (schema->second.apply_policy == SettingApplyPolicy::RestartRequired)
        {
            return {{SettingsResultStatus::InvalidValues,
                     "restart-required sections do not bind a live provider"}, {}};
        }
        if (providers_.contains(section_key))
        {
            return {{SettingsResultStatus::InvalidValues,
                     "a settings provider is already bound to section '" +
                         std::string{section_key} + "'"}, {}};
        }

        auto registration = std::make_shared<detail::SettingsProviderRegistrationState>();
        registration->generation = next_provider_generation_++;
        registration->section_key = std::string{section_key};
        registration->provider.store(&provider, std::memory_order_release);
        providers_.emplace(registration->section_key,
                           ProviderBinding{registration});
        return {{}, SettingsProviderRegistration{registration}};
    }

    SettingsEditResult SettingsRegistry::SubmitEdit(
        std::string_view section_key,
        uint64_t base_revision,
        SettingValues changed_values,
        SettingsEditOrigin origin)
    {
        const auto invalid = [](SettingsEditStatus status, std::string diagnostic)
        {
            return SettingsEditResult{0, 0, status, std::move(diagnostic)};
        };
        if (!IsSealed())
        {
            return invalid(SettingsEditStatus::InvalidValues,
                           "settings schema catalog must be sealed before editing");
        }
        if (changed_values.empty())
        {
            return invalid(SettingsEditStatus::InvalidValues,
                           "settings edit must change at least one property");
        }

        const auto schema = FindSection(section_key);
        if (!schema)
        {
            return invalid(SettingsEditStatus::InvalidValues,
                           "settings section '" + std::string{section_key} + "' is not registered");
        }
        const SettingsResolvedSnapshot current = GetResolvedSnapshot();
        const auto current_section = std::find_if(
            current.sections.begin(), current.sections.end(),
            [section_key](const ResolvedSettingsSection &candidate)
            {
                return candidate.key == section_key;
            });
        if (current_section == current.sections.end())
        {
            return invalid(SettingsEditStatus::InvalidValues,
                           "resolved settings section is unavailable");
        }
        if (current_section->revision != base_revision)
        {
            return {0, current_section->revision, SettingsEditStatus::StaleRevision,
                    "settings section changed since the edit draft was created"};
        }

        SettingValues candidate_values;
        for (const auto &[property_key, value] : current_section->properties)
        {
            candidate_values.emplace(property_key, value.value);
        }
        for (const auto &[property_key, value] : changed_values)
        {
            const auto property = candidate_values.find(property_key);
            if (property == candidate_values.end())
            {
                return invalid(SettingsEditStatus::InvalidValues,
                               "unknown settings property '" + property_key + "'");
            }
            property->second = value;
        }
        if (const SettingsResult result = ValidateValues(section_key, candidate_values); !result)
        {
            return invalid(SettingsEditStatus::InvalidValues, result.diagnostic);
        }

        std::lock_guard lock(mutex_);
        SweepInactiveProvidersLocked();
        const auto latest_revision = section_revisions_.find(section_key);
        const uint64_t current_revision = latest_revision == section_revisions_.end()
                                              ? 0
                                              : latest_revision->second;
        if (current_revision != base_revision)
        {
            return {0, current_revision, SettingsEditStatus::StaleRevision,
                    "settings section changed while the edit was being validated"};
        }

        if (schema->apply_policy == SettingApplyPolicy::RestartRequired)
        {
            SettingValues &session_values = session_overrides_[std::string{section_key}];
            for (const auto &[property_key, value] : changed_values)
            {
                session_values[property_key] = value;
            }
            MarkSectionRevisionsLocked({std::string{section_key}});
            return {0, section_revisions_[std::string{section_key}],
                    SettingsEditStatus::RestartRequired,
                    "setting is accepted for the next launch"};
        }

        if (section_in_flight_.contains(section_key))
        {
            return {0, current_revision, SettingsEditStatus::Busy,
                    "a settings edit for this section is already pending"};
        }
        const auto provider = providers_.find(section_key);
        if (provider == providers_.end() ||
            !provider->second.registration->active.load(std::memory_order_acquire))
        {
            return {0, current_revision, SettingsEditStatus::ProviderUnavailable,
                    "no live settings provider is bound to this section"};
        }
        if (provider_queue_.size() >= kMaximumQueuedSettingsRequests ||
            !TrimRequestResultsLocked())
        {
            return {0, current_revision, SettingsEditStatus::Busy,
                    "settings request or completion capacity is full"};
        }

        const uint64_t request_id = next_request_id_++;
        SettingsApplyRequest request{};
        request.request_id = request_id;
        request.registration_generation = provider->second.registration->generation;
        request.base_revision = base_revision;
        request.section_key = std::string{section_key};
        request.changed_values = std::move(changed_values);
        request.candidate_values = std::move(candidate_values);
        request.origin = origin;
        SettingsEditResult result{request_id, current_revision, SettingsEditStatus::Pending, {}};
        requests_.emplace(request_id, RequestRecord{std::move(request), result});
        provider_queue_.push_back(request_id);
        section_in_flight_.emplace(std::string{section_key}, request_id);
        return result;
    }

    bool SettingsRegistry::TrimRequestResultsLocked()
    {
        while (requests_.size() >= kMaximumRetainedSettingsResults)
        {
            const auto completed = std::find_if(
                requests_.begin(), requests_.end(), [](const auto &entry)
                {
                    return entry.second.result.status != SettingsEditStatus::Pending;
                });
            if (completed == requests_.end())
            {
                return false;
            }
            requests_.erase(completed);
        }
        return true;
    }

    void SettingsRegistry::SweepInactiveProvidersLocked()
    {
        for (auto provider = providers_.begin(); provider != providers_.end();)
        {
            if (provider->second.registration->active.load(std::memory_order_acquire))
            {
                ++provider;
                continue;
            }

            const auto pending = section_in_flight_.find(provider->first);
            if (pending != section_in_flight_.end())
            {
                const auto request = requests_.find(pending->second);
                if (request != requests_.end() &&
                    request->second.result.status == SettingsEditStatus::Pending)
                {
                    request->second.result.status = SettingsEditStatus::Cancelled;
                    request->second.result.diagnostic = "settings provider was unregistered";
                }
                section_in_flight_.erase(pending);
            }
            provider = providers_.erase(provider);
        }
    }

    std::size_t SettingsRegistry::PumpProviders(std::size_t maximum_requests)
    {
        std::size_t dispatched = 0;
        while (dispatched < maximum_requests)
        {
            uint64_t request_id = 0;
            SettingsApplyRequest request;
            ISettingsProvider *provider = nullptr;
            {
                std::lock_guard lock(mutex_);
                SweepInactiveProvidersLocked();
                while (!provider_queue_.empty())
                {
                    request_id = provider_queue_.front();
                    provider_queue_.pop_front();
                    const auto request_record = requests_.find(request_id);
                    if (request_record == requests_.end() ||
                        request_record->second.result.status != SettingsEditStatus::Pending)
                    {
                        request_id = 0;
                        continue;
                    }
                    const auto binding = providers_.find(request_record->second.request.section_key);
                    if (binding == providers_.end() ||
                        binding->second.registration->generation !=
                            request_record->second.request.registration_generation ||
                        !binding->second.registration->active.load(std::memory_order_acquire))
                    {
                        request_record->second.result.status = SettingsEditStatus::Cancelled;
                        request_record->second.result.diagnostic =
                            "settings provider is no longer registered";
                        section_in_flight_.erase(request_record->second.request.section_key);
                        request_id = 0;
                        continue;
                    }
                    request = request_record->second.request;
                    provider = binding->second.registration->provider.load(
                        std::memory_order_acquire);
                    if (provider == nullptr)
                    {
                        request_record->second.result.status = SettingsEditStatus::Cancelled;
                        request_record->second.result.diagnostic =
                            "settings provider was unregistered before dispatch";
                        section_in_flight_.erase(request_record->second.request.section_key);
                        request_id = 0;
                        continue;
                    }
                    break;
                }
            }
            if (request_id == 0 || provider == nullptr)
            {
                break;
            }

            SettingsCompletionHandle completion =
                MakeCompletionHandle(request.registration_generation, request.request_id);
            try
            {
                provider->Apply(request, completion);
            }
            catch (const std::exception &exception)
            {
                completion.Complete({SettingsProviderOutcome::Rejected, {}, exception.what()});
            }
            catch (...)
            {
                completion.Complete({SettingsProviderOutcome::Rejected, {},
                                     "settings provider threw an unknown exception"});
            }
            ++dispatched;
        }
        return dispatched;
    }

    SettingsCompletionHandle SettingsRegistry::MakeCompletionHandle(
        uint64_t generation, uint64_t request_id)
    {
        const std::weak_ptr<CompletionControl> weak_control = completion_control_;
        return SettingsCompletionHandle{
            [weak_control, generation, request_id](SettingsProviderResult result)
            {
                const auto control = weak_control.lock();
                if (!control)
                {
                    return false;
                }
                std::lock_guard lock(control->mutex);
                if (control->registry == nullptr)
                {
                    return false;
                }
                return control->registry->CompleteProviderEdit(
                    generation, request_id, std::move(result));
            }};
    }

    bool SettingsRegistry::CompleteProviderEdit(uint64_t generation,
                                               uint64_t request_id,
                                               SettingsProviderResult result)
    {
        SettingsApplyRequest request;
        SettingSectionDefinition schema;
        {
            std::lock_guard lock(mutex_);
            SweepInactiveProvidersLocked();
            const auto found = requests_.find(request_id);
            if (found == requests_.end() ||
                found->second.result.status != SettingsEditStatus::Pending ||
                found->second.request.registration_generation != generation)
            {
                return false;
            }
            const auto binding = providers_.find(found->second.request.section_key);
            if (binding == providers_.end() ||
                binding->second.registration->generation != generation ||
                !binding->second.registration->active.load(std::memory_order_acquire))
            {
                found->second.result.status = SettingsEditStatus::Cancelled;
                found->second.result.diagnostic = "settings provider was unregistered";
                section_in_flight_.erase(found->second.request.section_key);
                return false;
            }
            request = found->second.request;
            schema = sections_.at(request.section_key);
        }

        if (result.outcome == SettingsProviderOutcome::Applied)
        {
            if (result.active_values.empty())
            {
                result.active_values = request.candidate_values;
            }
            if (const SettingsResult validation =
                    ValidateSectionValues(schema, result.active_values);
                !validation)
            {
                result.outcome = SettingsProviderOutcome::Rejected;
                result.diagnostic = "provider reported invalid active values: " +
                                    validation.diagnostic;
                result.active_values.clear();
            }
        }

        std::lock_guard lock(mutex_);
        SweepInactiveProvidersLocked();
        const auto found = requests_.find(request_id);
        if (found == requests_.end() ||
            found->second.result.status != SettingsEditStatus::Pending ||
            found->second.request.registration_generation != generation)
        {
            return false;
        }
        const auto binding = providers_.find(found->second.request.section_key);
        if (binding == providers_.end() ||
            binding->second.registration->generation != generation ||
            !binding->second.registration->active.load(std::memory_order_acquire))
        {
            found->second.result.status = SettingsEditStatus::Cancelled;
            found->second.result.diagnostic = "settings provider was unregistered";
            section_in_flight_.erase(found->second.request.section_key);
            return false;
        }

        const auto section_revision = section_revisions_.find(request.section_key);
        const uint64_t current_revision = section_revision == section_revisions_.end()
                                              ? 0
                                              : section_revision->second;
        if (current_revision != request.base_revision)
        {
            found->second.result.status = SettingsEditStatus::Superseded;
            found->second.result.diagnostic =
                "settings changed while the provider was applying the request";
        }
        else if (result.outcome == SettingsProviderOutcome::Applied)
        {
            SettingValues &session_values = session_overrides_[request.section_key];
            for (const auto &[property_key, requested_value] : request.changed_values)
            {
                (void)requested_value;
                session_values[property_key] = result.active_values.at(property_key);
            }
            MarkSectionRevisionsLocked({request.section_key});
            found->second.result.status = SettingsEditStatus::Applied;
            found->second.result.section_revision = section_revisions_[request.section_key];
            found->second.result.diagnostic = std::move(result.diagnostic);
            found->second.result.active_values = std::move(result.active_values);
        }
        else if (result.outcome == SettingsProviderOutcome::Superseded)
        {
            found->second.result.status = SettingsEditStatus::Superseded;
            found->second.result.diagnostic = std::move(result.diagnostic);
        }
        else
        {
            found->second.result.status = SettingsEditStatus::Rejected;
            found->second.result.diagnostic = std::move(result.diagnostic);
        }
        section_in_flight_.erase(request.section_key);
        return true;
    }

    std::optional<SettingsEditResult> SettingsRegistry::GetEditResult(uint64_t request_id)
    {
        std::lock_guard lock(mutex_);
        SweepInactiveProvidersLocked();
        const auto found = requests_.find(request_id);
        if (found == requests_.end())
        {
            return std::nullopt;
        }
        return found->second.result;
    }
}
