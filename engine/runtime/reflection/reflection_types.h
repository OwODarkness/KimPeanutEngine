#ifndef KPENGINE_RUNTIME_REFLECTION_TYPES_H
#define KPENGINE_RUNTIME_REFLECTION_TYPES_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace kpengine::reflection
{
    using ReflectionHashFunction = uint32_t (*)(std::string_view);

    constexpr uint32_t DefaultReflectionHash(std::string_view value) noexcept
    {
        uint32_t hash = 2166136261u;
        for (const unsigned char character : value)
        {
            hash ^= character;
            hash *= 16777619u;
        }
        return hash;
    }

    struct ReflectionTypeId
    {
        uint32_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }
        constexpr explicit operator bool() const noexcept { return IsValid(); }

        friend constexpr bool operator==(ReflectionTypeId lhs, ReflectionTypeId rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        friend constexpr bool operator!=(ReflectionTypeId lhs, ReflectionTypeId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    struct ReflectionPropertyId
    {
        uint32_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }
        constexpr explicit operator bool() const noexcept { return IsValid(); }

        friend constexpr bool operator==(ReflectionPropertyId lhs,
                                         ReflectionPropertyId rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        friend constexpr bool operator!=(ReflectionPropertyId lhs,
                                         ReflectionPropertyId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    enum class ReflectionValueType : uint8_t
    {
        Bool,
        SignedInteger,
        UnsignedInteger,
        FloatingPoint,
        String,
    };

    class ReflectionValue
    {
    public:
        ReflectionValue() : value_(false) {}
        ReflectionValue(bool value) : value_(value) {}

        template <typename T,
                  std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                                       std::is_signed_v<T>,
                                   int> = 0>
        ReflectionValue(T value) : value_(static_cast<int64_t>(value))
        {
        }

        template <typename T,
                  std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                                       std::is_unsigned_v<T>,
                                   int> = 0>
        ReflectionValue(T value) : value_(static_cast<uint64_t>(value))
        {
        }

        ReflectionValue(float value) : value_(static_cast<double>(value)) {}
        ReflectionValue(double value) : value_(value) {}
        ReflectionValue(const char *value) : value_(std::string(value != nullptr ? value : "")) {}
        ReflectionValue(std::string value) : value_(std::move(value)) {}
        ReflectionValue(std::string_view value) : value_(std::string(value)) {}

        ReflectionValueType GetType() const noexcept
        {
            switch (value_.index())
            {
            case 0:
                return ReflectionValueType::Bool;
            case 1:
                return ReflectionValueType::SignedInteger;
            case 2:
                return ReflectionValueType::UnsignedInteger;
            case 3:
                return ReflectionValueType::FloatingPoint;
            default:
                return ReflectionValueType::String;
            }
        }

        template <typename T>
        const T *TryGet() const noexcept
        {
            return std::get_if<T>(&value_);
        }

        friend bool operator==(const ReflectionValue &lhs, const ReflectionValue &rhs)
        {
            return lhs.value_ == rhs.value_;
        }

        friend bool operator!=(const ReflectionValue &lhs, const ReflectionValue &rhs)
        {
            return !(lhs == rhs);
        }

    private:
        std::variant<bool, int64_t, uint64_t, double, std::string> value_;
    };

    enum class ReflectionPropertyFlags : uint8_t
    {
        None = 0,
        Readable = 1u << 0u,
        Writable = 1u << 1u,
        EditorVisible = 1u << 2u,
    };

    constexpr ReflectionPropertyFlags operator|(ReflectionPropertyFlags lhs,
                                                 ReflectionPropertyFlags rhs) noexcept
    {
        return static_cast<ReflectionPropertyFlags>(static_cast<uint8_t>(lhs) |
                                                    static_cast<uint8_t>(rhs));
    }

    constexpr ReflectionPropertyFlags operator&(ReflectionPropertyFlags lhs,
                                                 ReflectionPropertyFlags rhs) noexcept
    {
        return static_cast<ReflectionPropertyFlags>(static_cast<uint8_t>(lhs) &
                                                    static_cast<uint8_t>(rhs));
    }

    constexpr ReflectionPropertyFlags &operator|=(ReflectionPropertyFlags &lhs,
                                                   ReflectionPropertyFlags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    constexpr bool HasFlag(ReflectionPropertyFlags flags,
                           ReflectionPropertyFlags flag) noexcept
    {
        return (flags & flag) != ReflectionPropertyFlags::None;
    }

    enum class ReflectionResultStatus : uint8_t
    {
        Success,
        AlreadyInitialized,
        NotInitialized,
        InvalidArgument,
        InvalidObject,
        WrongObjectType,
        NotReadable,
        UnknownType,
        UnknownProperty,
        TypeMismatch,
        UnsupportedValue,
        ReadOnly,
        SetterRejected,
        DuplicateName,
        IdCollision,
        Frozen,
        ShutDown,
        InvalidDescriptor,
    };

    struct ReflectionResult
    {
        ReflectionResultStatus status = ReflectionResultStatus::Success;
        std::string diagnostic;

        bool IsSuccess() const noexcept { return status == ReflectionResultStatus::Success; }
        explicit operator bool() const noexcept { return IsSuccess(); }
    };

    struct ReflectionReadResult : ReflectionResult
    {
        ReflectionValue value;
    };

    struct ReflectionTypeDescriptor;

    struct ReflectionPropertyDescriptor
    {
        ReflectionPropertyId id;
        std::string name;
        ReflectionValueType value_type = ReflectionValueType::Bool;
        ReflectionPropertyFlags flags = ReflectionPropertyFlags::None;
    };

    struct ReflectionTypeDescriptor
    {
        ReflectionTypeId id;
        std::string name;
        std::vector<ReflectionPropertyDescriptor> properties;
    };

    class ReflectionObjectRef
    {
    public:
        template <typename T>
        static ReflectionObjectRef ForMutable(ReflectionTypeId type, T *object) noexcept
        {
            static_assert(!std::is_const_v<T>, "mutable reflection objects cannot be const");
            return ReflectionObjectRef{type, typeid(T), object, object};
        }

        template <typename T>
        static ReflectionObjectRef ForConst(ReflectionTypeId type, const T *object) noexcept
        {
            return ReflectionObjectRef{type, typeid(T), object, nullptr};
        }

        bool IsValid() const noexcept
        {
            return object_ != nullptr && type_.IsValid() && cpp_type_ != nullptr;
        }
        bool IsMutable() const noexcept { return mutable_object_ != nullptr; }
        bool IsOwnedByCurrentThread() const noexcept
        {
            return owner_thread_ == std::this_thread::get_id();
        }
        ReflectionTypeId GetType() const noexcept { return type_; }

    private:
        ReflectionObjectRef(ReflectionTypeId type,
                            const std::type_info &cpp_type,
                            const void *object,
                            void *mutable_object) noexcept
            : type_(type), cpp_type_(&cpp_type), object_(object), mutable_object_(mutable_object),
              owner_thread_(std::this_thread::get_id())
        {
        }

        ReflectionTypeId type_;
        const std::type_info *cpp_type_ = nullptr;
        const void *object_ = nullptr;
        void *mutable_object_ = nullptr;
        std::thread::id owner_thread_;

        friend class EnttReflectionRegistry;
    };

    template <typename T>
    using ReflectionRemoveCvRefT = std::remove_cv_t<std::remove_reference_t<T>>;

    template <typename T>
    constexpr bool IsSupportedReflectionValueType() noexcept
    {
        using Value = ReflectionRemoveCvRefT<T>;
        return std::is_same_v<Value, bool> ||
               (std::is_integral_v<Value> && !std::is_same_v<Value, bool>) ||
               std::is_floating_point_v<Value> || std::is_same_v<Value, std::string>;
    }

    template <typename T>
    constexpr ReflectionValueType GetReflectionValueType() noexcept
    {
        using Value = ReflectionRemoveCvRefT<T>;
        if constexpr (std::is_same_v<Value, bool>)
        {
            return ReflectionValueType::Bool;
        }
        else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>)
        {
            return ReflectionValueType::SignedInteger;
        }
        else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value>)
        {
            return ReflectionValueType::UnsignedInteger;
        }
        else if constexpr (std::is_floating_point_v<Value>)
        {
            return ReflectionValueType::FloatingPoint;
        }
        else
        {
            return ReflectionValueType::String;
        }
    }

    template <typename T>
    std::optional<ReflectionRemoveCvRefT<T>> ConvertReflectionValue(const ReflectionValue &value)
    {
        using Value = ReflectionRemoveCvRefT<T>;
        if constexpr (!IsSupportedReflectionValueType<Value>())
        {
            return std::nullopt;
        }
        else if constexpr (std::is_same_v<Value, bool>)
        {
            if (const bool *converted = value.TryGet<bool>())
            {
                return *converted;
            }
            return std::nullopt;
        }
        else if constexpr (std::is_same_v<Value, std::string>)
        {
            if (const std::string *converted = value.TryGet<std::string>())
            {
                return *converted;
            }
            return std::nullopt;
        }
        else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>)
        {
            if (const int64_t *converted = value.TryGet<int64_t>())
            {
                if (*converted >= static_cast<int64_t>(std::numeric_limits<Value>::lowest()) &&
                    *converted <= static_cast<int64_t>(std::numeric_limits<Value>::max()))
                {
                    return static_cast<Value>(*converted);
                }
            }
            if (const uint64_t *converted = value.TryGet<uint64_t>())
            {
                if (*converted <= static_cast<uint64_t>(std::numeric_limits<Value>::max()))
                {
                    return static_cast<Value>(*converted);
                }
            }
            if (const double *converted = value.TryGet<double>(); converted != nullptr &&
                std::isfinite(*converted) && std::trunc(*converted) == *converted)
            {
                const double upper_exclusive =
                    std::ldexp(1.0, std::numeric_limits<Value>::digits);
                if (*converted >= -upper_exclusive && *converted < upper_exclusive)
                {
                    return static_cast<Value>(*converted);
                }
            }
            return std::nullopt;
        }
        else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value>)
        {
            if (const uint64_t *converted = value.TryGet<uint64_t>())
            {
                if (*converted <= static_cast<uint64_t>(std::numeric_limits<Value>::max()))
                {
                    return static_cast<Value>(*converted);
                }
            }
            if (const int64_t *converted = value.TryGet<int64_t>())
            {
                if (*converted >= 0 &&
                    static_cast<uint64_t>(*converted) <= static_cast<uint64_t>(std::numeric_limits<Value>::max()))
                {
                    return static_cast<Value>(*converted);
                }
            }
            if (const double *converted = value.TryGet<double>(); converted != nullptr &&
                std::isfinite(*converted) && std::trunc(*converted) == *converted)
            {
                const double upper_exclusive =
                    std::ldexp(1.0, std::numeric_limits<Value>::digits);
                if (*converted >= 0.0 && *converted < upper_exclusive)
                {
                    return static_cast<Value>(*converted);
                }
            }
            return std::nullopt;
        }
        else
        {
            if (const double *converted = value.TryGet<double>(); converted != nullptr &&
                std::isfinite(*converted))
            {
                if constexpr (std::is_same_v<Value, float>)
                {
                    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
                    if (*converted < -maximum || *converted > maximum)
                    {
                        return std::nullopt;
                    }
                }
                const Value result = static_cast<Value>(*converted);
                return std::isfinite(result) ? std::optional<Value>{result} : std::nullopt;
            }
            if (const int64_t *converted = value.TryGet<int64_t>())
            {
                const Value result = static_cast<Value>(*converted);
                return std::isfinite(result) ? std::optional<Value>{result} : std::nullopt;
            }
            if (const uint64_t *converted = value.TryGet<uint64_t>())
            {
                const Value result = static_cast<Value>(*converted);
                return std::isfinite(result) ? std::optional<Value>{result} : std::nullopt;
            }
            return std::nullopt;
        }
    }

    template <typename T>
    ReflectionValue MakeReflectionValue(const T &value)
    {
        using Value = ReflectionRemoveCvRefT<T>;
        if constexpr (std::is_same_v<Value, bool>)
        {
            return ReflectionValue{value};
        }
        else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>)
        {
            return ReflectionValue{static_cast<int64_t>(value)};
        }
        else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value>)
        {
            return ReflectionValue{static_cast<uint64_t>(value)};
        }
        else if constexpr (std::is_floating_point_v<Value>)
        {
            return ReflectionValue{static_cast<double>(value)};
        }
        else
        {
            return ReflectionValue{std::string(value)};
        }
    }
}

#endif
