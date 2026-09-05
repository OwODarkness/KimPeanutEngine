#include "entt_reflection_registry.h"

#include <algorithm>
#include <utility>

namespace kpengine::reflection
{
    namespace
    {
        ReflectionResult MakeFailure(ReflectionResultStatus status, std::string diagnostic)
        {
            return {status, std::move(diagnostic)};
        }

        bool IsNumericValueType(ReflectionValueType value_type) noexcept
        {
            return value_type == ReflectionValueType::SignedInteger ||
                   value_type == ReflectionValueType::UnsignedInteger ||
                   value_type == ReflectionValueType::FloatingPoint;
        }

        bool HasMetadataContent(const ReflectionPropertyMetadata &metadata) noexcept
        {
            return !metadata.display_name.empty() || !metadata.category.empty() ||
                   !metadata.tooltip.empty() || metadata.semantic != ReflectionWidgetSemantic::Default ||
                   metadata.minimum.has_value() || metadata.maximum.has_value() ||
                   metadata.step.has_value() || !metadata.enum_options.empty();
        }

        ReflectionResult ValidateMetadata(ReflectionValueType value_type,
                                           ReflectionPropertyFlags flags,
                                           const ReflectionPropertyMetadata &metadata)
        {
            const bool numeric = IsNumericValueType(value_type);
            if (!numeric && (metadata.minimum.has_value() || metadata.maximum.has_value() ||
                             metadata.step.has_value()))
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "numeric metadata requires a numeric property");
            }
            if (metadata.minimum.has_value() && !std::isfinite(*metadata.minimum))
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "metadata minimum must be finite");
            }
            if (metadata.maximum.has_value() && !std::isfinite(*metadata.maximum))
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "metadata maximum must be finite");
            }
            if (metadata.step.has_value() &&
                (!std::isfinite(*metadata.step) || *metadata.step <= 0.0))
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "metadata step must be finite and positive");
            }
            if (metadata.minimum.has_value() && metadata.maximum.has_value() &&
                *metadata.minimum > *metadata.maximum)
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "metadata minimum cannot exceed maximum");
            }

            if (metadata.semantic != ReflectionWidgetSemantic::Default &&
                metadata.semantic != ReflectionWidgetSemantic::Enum && !numeric)
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "numeric widget semantics require a numeric property");
            }
            if (metadata.semantic == ReflectionWidgetSemantic::Enum &&
                value_type != ReflectionValueType::SignedInteger &&
                value_type != ReflectionValueType::UnsignedInteger)
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "enum widget semantics require an integer property");
            }
            if (!metadata.enum_options.empty() && metadata.semantic != ReflectionWidgetSemantic::Enum)
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "enum options require enum widget semantics");
            }
            if (metadata.semantic == ReflectionWidgetSemantic::Enum && metadata.enum_options.empty())
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "enum widget semantics require options");
            }
            for (std::size_t index = 0; index < metadata.enum_options.size(); ++index)
            {
                const ReflectionEnumOption &option = metadata.enum_options[index];
                if (option.label.empty())
                {
                    return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                       "enum option labels cannot be empty");
                }
                for (std::size_t previous = 0; previous < index; ++previous)
                {
                    const ReflectionEnumOption &other = metadata.enum_options[previous];
                    if (other.value == option.value)
                    {
                        return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                           "enum option values must be unique");
                    }
                    if (other.label == option.label)
                    {
                        return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                           "enum option labels must be unique");
                    }
                }
            }
            if (!HasFlag(flags, ReflectionPropertyFlags::EditorVisible) &&
                HasMetadataContent(metadata))
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "editor metadata requires an editor-visible property");
            }
            return {};
        }
    }

    EnttReflectionRegistry::EnttReflectionRegistry(ReflectionHashFunction hash_function)
        : hash_function_(hash_function != nullptr ? hash_function : DefaultReflectionHash)
    {
    }

    EnttReflectionRegistry::~EnttReflectionRegistry()
    {
        Shutdown();
    }

    ReflectionResult EnttReflectionRegistry::StateFailure() const
    {
        if (state_ == State::Frozen)
        {
            return MakeFailure(ReflectionResultStatus::Frozen,
                               "reflection registration is closed after freeze");
        }
        if (state_ == State::Registering)
        {
            return MakeFailure(ReflectionResultStatus::NotInitialized,
                               "reflection catalog has not been frozen");
        }
        return MakeFailure(ReflectionResultStatus::ShutDown,
                           "reflection registry has been shut down");
    }

    ReflectionTypeId EnttReflectionRegistry::MakeTypeId(std::string_view name) const noexcept
    {
        return {hash_function_(name)};
    }

    ReflectionPropertyId EnttReflectionRegistry::MakePropertyId(
        std::string_view type_name,
        std::string_view property_name) const
    {
        std::string canonical;
        canonical.reserve(type_name.size() + 2u + property_name.size());
        canonical.append(type_name);
        canonical.append("::");
        canonical.append(property_name);
        return {hash_function_(canonical)};
    }

    const char *EnttReflectionRegistry::StoreStableName(std::string_view name)
    {
        stable_names_.emplace_back(name);
        return stable_names_.back().c_str();
    }

    ReflectionResult EnttReflectionRegistry::BeginType(std::type_index cpp_type,
                                                       std::string_view name,
                                                       TypeRegistrationHandle &handle)
    {
        handle = {};
        if (state_ != State::Registering)
        {
            return StateFailure();
        }
        if (name.empty())
        {
            return MakeFailure(ReflectionResultStatus::InvalidArgument,
                               "reflected type name cannot be empty");
        }
        if (type_by_cpp_type_.find(cpp_type) != type_by_cpp_type_.end())
        {
            return MakeFailure(ReflectionResultStatus::DuplicateName,
                               "the C++ type has already been registered");
        }

        const ReflectionTypeId id = MakeTypeId(name);
        if (!id.IsValid())
        {
            return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                               "reflected type hash cannot be zero");
        }
        if (const auto existing = type_names_by_id_.find(id.value);
            existing != type_names_by_id_.end())
        {
            if (existing->second == name)
            {
                return MakeFailure(ReflectionResultStatus::DuplicateName,
                                   "the reflected type name is duplicated");
            }
            return MakeFailure(ReflectionResultStatus::IdCollision,
                               "two reflected type names have the same identifier");
        }

        const std::size_t index = types_.size();
        const char *stable_name = StoreStableName(name);
        TypeRecord record;
        record.descriptor.id = id;
        record.descriptor.name = std::string(name);
        record.cpp_type = cpp_type;
        types_.push_back(std::move(record));
        type_by_id_.emplace(id.value, index);
        type_names_by_id_.emplace(id.value, std::string(name));
        type_by_cpp_type_.emplace(cpp_type, index);

        handle.index = index;
        handle.stable_name = stable_name;
        return {};
    }

    ReflectionResult EnttReflectionRegistry::BeginProperty(
        std::size_t type_index,
        std::string_view name,
        ReflectionValueType value_type,
        ReflectionPropertyFlags flags,
        ReflectionPropertyMetadata metadata,
        ReadFunction read,
        WriteFunction write,
        PropertyRegistrationHandle &handle)
    {
        handle = {};
        if (state_ != State::Registering)
        {
            return StateFailure();
        }
        if (type_index >= types_.size() || name.empty() || !read)
        {
            return MakeFailure(ReflectionResultStatus::InvalidArgument,
                               "invalid reflected property declaration");
        }

        const TypeRecord &type = types_[type_index];
        const ReflectionPropertyId id = MakePropertyId(type.descriptor.name, name);
        if (!id.IsValid())
        {
            return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                               "reflected property hash cannot be zero");
        }
        for (const PropertyRecord &property : type.properties)
        {
            if (property.descriptor.name == name)
            {
                return MakeFailure(ReflectionResultStatus::DuplicateName,
                                   "the reflected property name is duplicated");
            }
            if (property.descriptor.id == id)
            {
                return MakeFailure(ReflectionResultStatus::IdCollision,
                                   "two properties on a type have the same identifier");
            }
        }

        const std::string canonical_name = type.descriptor.name + "::" + std::string(name);
        if (const auto existing = property_names_by_id_.find(id.value);
            existing != property_names_by_id_.end() && existing->second != canonical_name)
        {
            return MakeFailure(ReflectionResultStatus::IdCollision,
                               "two reflected properties have the same identifier");
        }

        if (read)
        {
            flags |= ReflectionPropertyFlags::Readable;
        }
        else
        {
            flags = static_cast<ReflectionPropertyFlags>(
                static_cast<uint8_t>(flags) &
                ~static_cast<uint8_t>(ReflectionPropertyFlags::Readable));
        }
        if (write)
        {
            flags |= ReflectionPropertyFlags::Writable;
        }
        else
        {
            flags = static_cast<ReflectionPropertyFlags>(
                static_cast<uint8_t>(flags) &
                ~static_cast<uint8_t>(ReflectionPropertyFlags::Writable));
        }

        const ReflectionResult metadata_result = ValidateMetadata(value_type, flags, metadata);
        if (!metadata_result)
        {
            return metadata_result;
        }

        const char *stable_name = StoreStableName(name);
        PropertyRecord record;
        record.descriptor = {id, std::string(name), value_type, flags, std::move(metadata)};
        record.read = std::move(read);
        record.write = std::move(write);
        types_[type_index].properties.push_back(std::move(record));
        property_names_by_id_.emplace(id.value, canonical_name);
        handle.stable_name = stable_name;
        return {};
    }

    void EnttReflectionRegistry::RebuildLookupTables()
    {
        type_by_id_.clear();
        type_by_cpp_type_.clear();
        for (std::size_t index = 0; index < types_.size(); ++index)
        {
            type_by_id_.emplace(types_[index].descriptor.id.value, index);
            type_by_cpp_type_.emplace(types_[index].cpp_type, index);
            std::sort(types_[index].properties.begin(), types_[index].properties.end(),
                      [](const PropertyRecord &lhs, const PropertyRecord &rhs) {
                          return lhs.descriptor.name < rhs.descriptor.name;
                      });
            types_[index].descriptor.properties.clear();
            for (const PropertyRecord &property : types_[index].properties)
            {
                types_[index].descriptor.properties.push_back(property.descriptor);
            }
        }
    }

    ReflectionResult EnttReflectionRegistry::Freeze()
    {
        if (state_ == State::Frozen)
        {
            return {};
        }
        if (state_ == State::ShutDown)
        {
            return StateFailure();
        }

        std::sort(types_.begin(), types_.end(), [](const TypeRecord &lhs, const TypeRecord &rhs) {
            return lhs.descriptor.name < rhs.descriptor.name;
        });
        for (const TypeRecord &type : types_)
        {
            if (!type.descriptor.id.IsValid() || type.descriptor.name.empty())
            {
                return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                   "reflected type descriptor is invalid");
            }
            for (const PropertyRecord &property : type.properties)
            {
                const bool readable = HasFlag(property.descriptor.flags,
                                              ReflectionPropertyFlags::Readable);
                const bool writable = HasFlag(property.descriptor.flags,
                                              ReflectionPropertyFlags::Writable);
                if (!property.descriptor.id.IsValid() || property.descriptor.name.empty() ||
                    readable != static_cast<bool>(property.read) ||
                    writable != static_cast<bool>(property.write))
                {
                    return MakeFailure(ReflectionResultStatus::InvalidDescriptor,
                                       "reflected property flags do not match its accessors");
                }
            }
        }
        RebuildLookupTables();
        state_ = State::Frozen;
        return {};
    }

    void EnttReflectionRegistry::Shutdown() noexcept
    {
        if (state_ == State::ShutDown)
        {
            return;
        }
        state_ = State::ShutDown;
        types_.clear();
        type_by_id_.clear();
        type_names_by_id_.clear();
        type_by_cpp_type_.clear();
        property_names_by_id_.clear();
        stable_names_.clear();
        entt::meta_reset(context_);
    }

    const ReflectionTypeDescriptor *EnttReflectionRegistry::FindType(ReflectionTypeId type) const noexcept
    {
        if (state_ != State::Frozen)
        {
            return nullptr;
        }
        const auto found = type_by_id_.find(type.value);
        return found == type_by_id_.end() ? nullptr : &types_[found->second].descriptor;
    }

    const ReflectionTypeDescriptor *EnttReflectionRegistry::FindType(std::string_view name) const noexcept
    {
        if (state_ != State::Frozen)
        {
            return nullptr;
        }
        for (const TypeRecord &type : types_)
        {
            if (type.descriptor.name == name)
            {
                return &type.descriptor;
            }
        }
        return nullptr;
    }

    const ReflectionPropertyDescriptor *EnttReflectionRegistry::FindProperty(
        ReflectionTypeId type,
        ReflectionPropertyId property) const noexcept
    {
        const ReflectionTypeDescriptor *descriptor = FindType(type);
        if (descriptor == nullptr)
        {
            return nullptr;
        }
        for (const ReflectionPropertyDescriptor &candidate : descriptor->properties)
        {
            if (candidate.id == property)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    std::vector<ReflectionTypeDescriptor> EnttReflectionRegistry::EnumerateTypes() const
    {
        std::vector<ReflectionTypeDescriptor> result;
        if (state_ != State::Frozen)
        {
            return result;
        }
        result.reserve(types_.size());
        for (const TypeRecord &type : types_)
        {
            result.push_back(type.descriptor);
        }
        return result;
    }

    ReflectionReadResult EnttReflectionRegistry::Read(const ReflectionObjectRef &object,
                                                      ReflectionPropertyId property) const
    {
        ReflectionReadResult result{};
        if (state_ != State::Frozen)
        {
            const ReflectionResult failure = StateFailure();
            result.status = failure.status;
            result.diagnostic = failure.diagnostic;
            return result;
        }
        if (!object.IsValid())
        {
            result.status = ReflectionResultStatus::InvalidObject;
            result.diagnostic = "reflection object is invalid";
            return result;
        }
        if (!object.IsOwnedByCurrentThread())
        {
            result.status = ReflectionResultStatus::InvalidObject;
            result.diagnostic = "reflection object is used from a different thread";
            return result;
        }

        const auto type_found = type_by_id_.find(object.GetType().value);
        if (type_found == type_by_id_.end())
        {
            result.status = ReflectionResultStatus::UnknownType;
            result.diagnostic = "reflection object type is not registered";
            return result;
        }
        const TypeRecord &type = types_[type_found->second];
        if (object.cpp_type_ == nullptr || std::type_index{*object.cpp_type_} != type.cpp_type)
        {
            result.status = ReflectionResultStatus::WrongObjectType;
            result.diagnostic = "reflection object address does not match its registered type";
            return result;
        }
        for (const PropertyRecord &candidate : type.properties)
        {
            if (candidate.descriptor.id == property)
            {
                if (!HasFlag(candidate.descriptor.flags, ReflectionPropertyFlags::Readable) ||
                    !candidate.read)
                {
                    result.status = ReflectionResultStatus::NotReadable;
                    result.diagnostic = "reflection property is not readable";
                    return result;
                }
                return candidate.read(object);
            }
        }
        result.status = ReflectionResultStatus::UnknownProperty;
        result.diagnostic = "reflection property is not registered on the object type";
        return result;
    }

    ReflectionResult EnttReflectionRegistry::Write(const ReflectionObjectRef &object,
                                                   ReflectionPropertyId property,
                                                   const ReflectionValue &value) const
    {
        if (state_ != State::Frozen)
        {
            return StateFailure();
        }
        if (!object.IsValid())
        {
            return MakeFailure(ReflectionResultStatus::InvalidObject,
                               "reflection object is invalid");
        }
        if (!object.IsOwnedByCurrentThread())
        {
            return MakeFailure(ReflectionResultStatus::InvalidObject,
                               "reflection object is used from a different thread");
        }

        const auto type_found = type_by_id_.find(object.GetType().value);
        if (type_found == type_by_id_.end())
        {
            return MakeFailure(ReflectionResultStatus::UnknownType,
                               "reflection object type is not registered");
        }
        const TypeRecord &type = types_[type_found->second];
        if (object.cpp_type_ == nullptr || std::type_index{*object.cpp_type_} != type.cpp_type)
        {
            return MakeFailure(ReflectionResultStatus::WrongObjectType,
                               "reflection object address does not match its registered type");
        }
        for (const PropertyRecord &candidate : type.properties)
        {
            if (candidate.descriptor.id == property)
            {
                if (!HasFlag(candidate.descriptor.flags, ReflectionPropertyFlags::Writable) ||
                    !candidate.write)
                {
                    return MakeFailure(ReflectionResultStatus::ReadOnly,
                                       "reflection property is read-only");
                }
                return candidate.write(object, value);
            }
        }
        return MakeFailure(ReflectionResultStatus::UnknownProperty,
                           "reflection property is not registered on the object type");
    }
}
