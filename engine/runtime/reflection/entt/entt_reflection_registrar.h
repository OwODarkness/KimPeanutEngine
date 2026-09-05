#ifndef KPENGINE_RUNTIME_REFLECTION_ENTT_REFLECTION_REGISTRAR_H
#define KPENGINE_RUNTIME_REFLECTION_ENTT_REFLECTION_REGISTRAR_H

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "entt_reflection_registry.h"

namespace kpengine::reflection
{
    namespace detail
    {
        template <typename T>
        struct MemberFunctionTraits;

        template <typename Return, typename Class>
        struct MemberFunctionTraits<Return (Class::*)() const>
        {
            using return_type = Return;
            using class_type = Class;
        };

        template <typename Return, typename Class>
        struct MemberFunctionTraits<Return (Class::*)()>
        {
            using return_type = Return;
            using class_type = Class;
        };

        template <typename Return, typename Class>
        struct MemberFunctionTraits<Return (*)(const Class &)>
        {
            using return_type = Return;
            using class_type = Class;
        };

        template <typename Return, typename Class>
        struct MemberFunctionTraits<Return (&)(const Class &)> :
            MemberFunctionTraits<Return (*)(const Class &)>
        {
        };

        template <typename T>
        struct MemberObjectTraits;

        template <typename Value, typename Class>
        struct MemberObjectTraits<Value Class::*>
        {
            using value_type = Value;
            using class_type = Class;
        };

        template <typename T>
        struct SetterFunctionTraits;

        template <typename Return, typename Class, typename Argument>
        struct SetterFunctionTraits<Return (Class::*)(Argument)>
        {
            using return_type = Return;
            using argument_type = Argument;
            using class_type = Class;
        };

        template <typename Return, typename Class, typename Argument>
        struct SetterFunctionTraits<Return (*)(Class &, Argument)>
        {
            using return_type = Return;
            using argument_type = Argument;
            using class_type = Class;
        };

        template <typename Return, typename Class, typename Argument>
        struct SetterFunctionTraits<Return (&)(Class &, Argument)> :
            SetterFunctionTraits<Return (*)(Class &, Argument)>
        {
        };
    }

    template <typename T>
    class EnttReflectionTypeRegistrar
    {
    public:
        EnttReflectionTypeRegistrar() = default;

        explicit operator bool() const noexcept { return result_.IsSuccess(); }
        const ReflectionResult &GetResult() const noexcept { return result_; }

        template <auto Member>
        ReflectionResult Property(
            std::string_view name,
            ReflectionPropertyFlags flags = ReflectionPropertyFlags::Readable |
                                            ReflectionPropertyFlags::Writable |
                                            ReflectionPropertyFlags::EditorVisible,
            ReflectionPropertyMetadata metadata = {})
        {
            if (!result_)
            {
                return result_;
            }
            using MemberType = typename detail::MemberObjectTraits<decltype(Member)>::value_type;
            if constexpr (!IsSupportedReflectionValueType<MemberType>())
            {
                return Remember({ReflectionResultStatus::UnsupportedValue,
                                 "property type is not supported by RF1"});
            }
            else
            {
                if constexpr (std::is_const_v<MemberType>)
                {
                    flags = static_cast<ReflectionPropertyFlags>(
                        static_cast<uint8_t>(flags) &
                        ~static_cast<uint8_t>(ReflectionPropertyFlags::Writable));
                }

                EnttReflectionRegistry::PropertyRegistrationHandle handle{};
                const ReflectionResult registered = registry_->BeginProperty(
                    type_index_, name, GetReflectionValueType<MemberType>(), flags,
                    std::move(metadata),
                    [registry = registry_, property = ReflectionPropertyId{property_id(name)}](
                        const ReflectionObjectRef &object) {
                        return registry->template ReadMetaProperty<T, MemberType>(object, property);
                    },
                    [registry = registry_, member = Member](const ReflectionObjectRef &object,
                                                            const ReflectionValue &value) {
                        return registry->template WriteMember<T, MemberType>(object, member, value);
                    },
                    handle);
                if (!registered)
                {
                    return Remember(registered);
                }

                entt::meta_factory<T>{registry_->GetContext()}.template data<Member>(
                    ReflectionPropertyId{property_id(name)}.value, handle.stable_name);
                return Remember(registered);
            }
        }

        template <auto Setter, auto Getter>
        ReflectionResult Property(
            std::string_view name,
            ReflectionPropertyFlags flags = ReflectionPropertyFlags::Readable |
                                            ReflectionPropertyFlags::Writable |
                                            ReflectionPropertyFlags::EditorVisible,
            ReflectionPropertyMetadata metadata = {})
        {
            if (!result_)
            {
                return result_;
            }
            using Value = typename detail::MemberFunctionTraits<decltype(Getter)>::return_type;
            using CleanValue = ReflectionRemoveCvRefT<Value>;
            using SetterTraits = detail::SetterFunctionTraits<decltype(Setter)>;
            using SetterValue =
                ReflectionRemoveCvRefT<typename SetterTraits::argument_type>;
            static_assert(std::is_invocable_v<decltype(Getter), const T &>,
                          "reflection getter must be invocable with the reflected type");
            static_assert(std::is_invocable_v<decltype(Setter), T &, SetterValue>,
                          "reflection setter must be invocable with the reflected type");
            static_assert(std::is_same_v<CleanValue, SetterValue>,
                          "reflection getter and setter value types must match");
            if constexpr (!IsSupportedReflectionValueType<CleanValue>())
            {
                return Remember({ReflectionResultStatus::UnsupportedValue,
                                 "property type is not supported by RF1"});
            }
            else
            {
                EnttReflectionRegistry::PropertyRegistrationHandle handle{};
                const ReflectionResult registered = registry_->BeginProperty(
                    type_index_, name, GetReflectionValueType<CleanValue>(), flags,
                    std::move(metadata),
                    [registry = registry_, property = ReflectionPropertyId{property_id(name)}](
                        const ReflectionObjectRef &object) {
                        return registry->template ReadMetaProperty<T, CleanValue>(object, property);
                    },
                    [registry = registry_](const ReflectionObjectRef &object,
                                           const ReflectionValue &value) {
                        return registry->template WriteSetter<T, Setter, CleanValue>(object, value);
                    },
                    handle);
                if (!registered)
                {
                    return Remember(registered);
                }

                entt::meta_factory<T>{registry_->GetContext()}.template data<Setter, Getter>(
                    ReflectionPropertyId{property_id(name)}.value, handle.stable_name);
                return Remember(registered);
            }
        }

        template <auto Getter>
        ReflectionResult ReadOnly(
            std::string_view name,
            ReflectionPropertyFlags flags = ReflectionPropertyFlags::Readable |
                                            ReflectionPropertyFlags::EditorVisible,
            ReflectionPropertyMetadata metadata = {})
        {
            if (!result_)
            {
                return result_;
            }
            using Value = typename detail::MemberFunctionTraits<decltype(Getter)>::return_type;
            using CleanValue = ReflectionRemoveCvRefT<Value>;
            static_assert(std::is_invocable_v<decltype(Getter), const T &>,
                          "reflection getter must be invocable with the reflected type");
            if constexpr (!IsSupportedReflectionValueType<CleanValue>())
            {
                return Remember({ReflectionResultStatus::UnsupportedValue,
                                 "property type is not supported by RF1"});
            }
            else
            {
                EnttReflectionRegistry::PropertyRegistrationHandle handle{};
                const ReflectionResult registered = registry_->BeginProperty(
                    type_index_, name, GetReflectionValueType<CleanValue>(), flags,
                    std::move(metadata),
                    [registry = registry_, property = ReflectionPropertyId{property_id(name)}](
                        const ReflectionObjectRef &object) {
                        return registry->template ReadMetaProperty<T, CleanValue>(object, property);
                    },
                    {}, handle);
                if (!registered)
                {
                    return Remember(registered);
                }

                entt::meta_factory<T>{registry_->GetContext()}.template data<nullptr, Getter>(
                    ReflectionPropertyId{property_id(name)}.value, handle.stable_name);
                return Remember(registered);
            }
        }

    private:
        EnttReflectionTypeRegistrar(EnttReflectionRegistry *registry,
                                    std::size_t type_index,
                                    ReflectionResult result)
            : registry_(registry), type_index_(type_index), result_(std::move(result))
        {
        }

        ReflectionResult Remember(ReflectionResult result)
        {
            result_ = result;
            return result_;
        }

        uint32_t property_id(std::string_view name) const
        {
            return registry_->MakePropertyId(registry_->types_[type_index_].descriptor.name, name).value;
        }

        EnttReflectionRegistry *registry_ = nullptr;
        std::size_t type_index_ = static_cast<std::size_t>(-1);
        ReflectionResult result_{ReflectionResultStatus::InvalidArgument,
                                 "type registration has not been created"};

        friend class EnttReflectionRegistrar;
    };

    class EnttReflectionRegistrar
    {
    public:
        explicit EnttReflectionRegistrar(EnttReflectionRegistry &registry) noexcept
            : registry_(registry)
        {
        }

        template <typename T>
        EnttReflectionTypeRegistrar<T> Type(std::string_view name)
        {
            EnttReflectionRegistry::TypeRegistrationHandle handle{};
            const ReflectionResult result = registry_.BeginType(typeid(T), name, handle);
            return EnttReflectionTypeRegistrar<T>{&registry_, handle.index, result};
        }

    private:
        EnttReflectionRegistry &registry_;
    };

    template <typename T, auto Setter, typename Value>
    ReflectionResult EnttReflectionRegistry::WriteSetter(const ReflectionObjectRef &object,
                                                         const ReflectionValue &value) const
    {
        ReflectionResult result{};
        if (!object.IsMutable())
        {
            result.status = ReflectionResultStatus::ReadOnly;
            result.diagnostic = "reflection object is const";
            return result;
        }

        using SetterTraits = detail::SetterFunctionTraits<decltype(Setter)>;
        using Argument = ReflectionRemoveCvRefT<typename SetterTraits::argument_type>;
        const std::optional<Argument> converted = ConvertReflectionValue<Argument>(value);
        if (!converted)
        {
            result.status = ReflectionResultStatus::TypeMismatch;
            result.diagnostic = "value cannot be converted to the setter argument type";
            return result;
        }

        if constexpr (std::is_same_v<typename SetterTraits::return_type, bool>)
        {
            if (!std::invoke(Setter, *static_cast<T *>(object.mutable_object_), *converted))
            {
                result.status = ReflectionResultStatus::SetterRejected;
                result.diagnostic = "the reflected setter rejected the value";
                return result;
            }
        }
        else
        {
            std::invoke(Setter, *static_cast<T *>(object.mutable_object_), *converted);
        }

        result.status = ReflectionResultStatus::Success;
        return result;
    }
}

#endif
