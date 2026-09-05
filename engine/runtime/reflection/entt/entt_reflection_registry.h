#ifndef KPENGINE_RUNTIME_REFLECTION_ENTT_REFLECTION_REGISTRY_H
#define KPENGINE_RUNTIME_REFLECTION_ENTT_REFLECTION_REGISTRY_H

#include <cstddef>
#include <functional>
#include <list>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

#include "../i_reflection_access.h"
#include "../i_reflection_catalog.h"

namespace kpengine::reflection
{
    class EnttReflectionRegistrar;
    template <typename>
    class EnttReflectionTypeRegistrar;

    class EnttReflectionRegistry final : public IReflectionCatalog, public IReflectionAccess
    {
    public:
        enum class State : uint8_t
        {
            Registering,
            Frozen,
            ShutDown,
        };

        using ReadFunction = std::function<ReflectionReadResult(const ReflectionObjectRef &)>;
        using WriteFunction = std::function<ReflectionResult(const ReflectionObjectRef &, const ReflectionValue &)>;

        struct TypeRegistrationHandle
        {
            std::size_t index = static_cast<std::size_t>(-1);
            const char *stable_name = nullptr;
        };

        struct PropertyRegistrationHandle
        {
            const char *stable_name = nullptr;
        };

        explicit EnttReflectionRegistry(ReflectionHashFunction hash_function = nullptr);
        ~EnttReflectionRegistry() override;

        EnttReflectionRegistry(const EnttReflectionRegistry &) = delete;
        EnttReflectionRegistry &operator=(const EnttReflectionRegistry &) = delete;
        EnttReflectionRegistry(EnttReflectionRegistry &&) = delete;
        EnttReflectionRegistry &operator=(EnttReflectionRegistry &&) = delete;

        entt::meta_ctx &GetContext() noexcept { return context_; }
        State GetState() const noexcept { return state_; }

        ReflectionResult BeginType(std::type_index cpp_type,
                                   std::string_view name,
                                   TypeRegistrationHandle &handle);
        ReflectionResult BeginProperty(std::size_t type_index,
                                       std::string_view name,
                                       ReflectionValueType value_type,
                                       ReflectionPropertyFlags flags,
                                       ReadFunction read,
                                       WriteFunction write,
                                       PropertyRegistrationHandle &handle);

        ReflectionResult Freeze();
        void Shutdown() noexcept;

        const ReflectionTypeDescriptor *FindType(ReflectionTypeId type) const noexcept override;
        const ReflectionTypeDescriptor *FindType(std::string_view name) const noexcept override;
        const ReflectionPropertyDescriptor *FindProperty(
            ReflectionTypeId type,
            ReflectionPropertyId property) const noexcept override;
        std::vector<ReflectionTypeDescriptor> EnumerateTypes() const override;

        ReflectionReadResult Read(const ReflectionObjectRef &object,
                                  ReflectionPropertyId property) const override;
        ReflectionResult Write(const ReflectionObjectRef &object,
                               ReflectionPropertyId property,
                               const ReflectionValue &value) const override;

    private:
        template <typename T, typename Value>
        ReflectionReadResult ReadMetaProperty(const ReflectionObjectRef &object,
                                              ReflectionPropertyId property) const
        {
            ReflectionReadResult result{};
            if (!object.IsValid())
            {
                result.status = ReflectionResultStatus::InvalidObject;
                result.diagnostic = "reflection object is invalid";
                return result;
            }

            const entt::meta_type type = entt::resolve<T>(context_);
            const entt::meta_any value =
                type.get(property.value, *static_cast<const T *>(object.object_));
            if (!value)
            {
                result.status = ReflectionResultStatus::TypeMismatch;
                result.diagnostic = "EnTT returned no value for the registered property";
                return result;
            }

            const Value *converted = value.try_cast<Value>();
            if (converted == nullptr)
            {
                result.status = ReflectionResultStatus::TypeMismatch;
                result.diagnostic = "EnTT returned an unexpected property value type";
                return result;
            }

            result.value = MakeReflectionValue(*converted);
            result.status = ReflectionResultStatus::Success;
            return result;
        }

        template <typename T, typename Member>
        ReflectionResult WriteMember(const ReflectionObjectRef &object,
                                     Member T::*member,
                                     const ReflectionValue &value) const
        {
            ReflectionResult result{};
            if (!object.IsMutable())
            {
                result.status = ReflectionResultStatus::ReadOnly;
                result.diagnostic = "reflection object is const";
                return result;
            }

            using Value = ReflectionRemoveCvRefT<Member>;
            const std::optional<Value> converted = ConvertReflectionValue<Value>(value);
            if (!converted)
            {
                result.status = ReflectionResultStatus::TypeMismatch;
                result.diagnostic = "value cannot be converted to the property type";
                return result;
            }

            if constexpr (std::is_const_v<Member>)
            {
                result.status = ReflectionResultStatus::ReadOnly;
                result.diagnostic = "property is const";
            }
            else
            {
                static_cast<T *>(object.mutable_object_)->*member = *converted;
                result.status = ReflectionResultStatus::Success;
            }
            return result;
        }

        template <typename T, auto Setter, typename Value>
        ReflectionResult WriteSetter(const ReflectionObjectRef &object,
                                     const ReflectionValue &value) const;

        struct PropertyRecord
        {
            ReflectionPropertyDescriptor descriptor;
            ReadFunction read;
            WriteFunction write;
        };

        struct TypeRecord
        {
            ReflectionTypeDescriptor descriptor;
            std::type_index cpp_type{typeid(void)};
            std::vector<PropertyRecord> properties;
        };

        ReflectionResult StateFailure() const;
        ReflectionTypeId MakeTypeId(std::string_view name) const noexcept;
        ReflectionPropertyId MakePropertyId(std::string_view type_name,
                                            std::string_view property_name) const;
        const char *StoreStableName(std::string_view name);
        void RebuildLookupTables();

        entt::meta_ctx context_;
        State state_ = State::Registering;
        ReflectionHashFunction hash_function_ = DefaultReflectionHash;
        std::list<std::string> stable_names_;
        std::vector<TypeRecord> types_;
        std::unordered_map<uint32_t, std::size_t> type_by_id_;
        std::unordered_map<uint32_t, std::string> type_names_by_id_;
        std::unordered_map<std::type_index, std::size_t> type_by_cpp_type_;
        std::unordered_map<uint32_t, std::string> property_names_by_id_;

        friend class EnttReflectionRegistrar;
        template <typename>
        friend class EnttReflectionTypeRegistrar;
    };
}

#endif
