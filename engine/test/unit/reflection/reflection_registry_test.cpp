#include <gtest/gtest.h>

#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

#include "reflection/entt/entt_reflection_registrar.h"
#include "reflection/reflection_system.h"

namespace
{
    using namespace kpengine::reflection;

    struct TestComponent
    {
        int value = 7;
        std::string label = "initial";
        int guarded = 1;

        int GetGuarded() const { return guarded; }
        bool SetGuarded(int candidate)
        {
            if (candidate < 0)
            {
                return false;
            }
            guarded = candidate;
            return true;
        }

        std::string GetLabel() const { return label; }
    };

    struct OtherComponent
    {
        int value = 0;
    };

    struct NumericComponent
    {
        int64_t signed_value = 0;
        uint64_t unsigned_value = 0;
        float ratio = 0.0F;
        double precise = 0.0;
        bool enabled = false;
    };

    ReflectionResult RegisterTestComponent(EnttReflectionRegistrar &registrar)
    {
        auto type = registrar.Type<TestComponent>("tests::TestComponent");
        if (!type)
        {
            return type.GetResult();
        }
        if (const ReflectionResult result = type.Property<&TestComponent::value>("value"); !result)
        {
            return result;
        }
        if (const ReflectionResult result = type.Property<&TestComponent::label>("label"); !result)
        {
            return result;
        }
        if (const ReflectionResult result =
                type.Property<&TestComponent::SetGuarded, &TestComponent::GetGuarded>("guarded");
            !result)
        {
            return result;
        }
        return type.ReadOnly<&TestComponent::GetLabel>(
            "display_label", ReflectionPropertyFlags::Readable |
                                 ReflectionPropertyFlags::Writable |
                                 ReflectionPropertyFlags::EditorVisible);
    }

    ReflectionResult RegisterNumericComponent(EnttReflectionRegistrar &registrar)
    {
        auto type = registrar.Type<NumericComponent>("tests::NumericComponent");
        if (!type)
        {
            return type.GetResult();
        }
        if (const ReflectionResult result =
                type.Property<&NumericComponent::signed_value>("signed_value");
            !result)
        {
            return result;
        }
        if (const ReflectionResult result =
                type.Property<&NumericComponent::unsigned_value>("unsigned_value");
            !result)
        {
            return result;
        }
        if (const ReflectionResult result = type.Property<&NumericComponent::ratio>("ratio");
            !result)
        {
            return result;
        }
        if (const ReflectionResult result =
                type.Property<&NumericComponent::precise>("precise");
            !result)
        {
            return result;
        }
        return type.Property<&NumericComponent::enabled>("enabled");
    }

    const ReflectionTypeDescriptor *GetTestType(const IReflectionCatalog &catalog)
    {
        return catalog.FindType("tests::TestComponent");
    }

    const ReflectionPropertyDescriptor *GetProperty(const ReflectionTypeDescriptor &type,
                                                     std::string_view name)
    {
        for (const ReflectionPropertyDescriptor &property : type.properties)
        {
            if (property.name == name)
            {
                return &property;
            }
        }
        return nullptr;
    }
}

TEST(ReflectionRegistry, UsesAnExplicitContextAndPublishesDeterministicCatalog)
{
    entt::meta_reset();
    entt::meta_factory<OtherComponent>{}.type("default::OtherComponent");

    ReflectionSystem system;
    const std::vector<ReflectionRegistrationFunction> registrations{RegisterTestComponent};
    ASSERT_TRUE(system.Initialize(registrations));
    ASSERT_EQ(system.GetState(), ReflectionSystem::State::Frozen);
    ASSERT_NE(system.GetCatalog(), nullptr);
    ASSERT_NE(system.GetAccess(), nullptr);

    const IReflectionCatalog &catalog = *system.GetCatalog();
    const ReflectionTypeDescriptor *type = GetTestType(catalog);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name, "tests::TestComponent");
    EXPECT_EQ(catalog.FindType("default::OtherComponent"), nullptr);

    const std::vector<ReflectionTypeDescriptor> descriptors = catalog.EnumerateTypes();
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().name, "tests::TestComponent");
    EXPECT_EQ(descriptors.front().properties.front().name, "display_label");

    system.Shutdown();
    entt::meta_reset();
}

TEST(ReflectionRegistry, ReadsWritesAndReportsStructuredFailures)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({RegisterTestComponent}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();
    const ReflectionTypeDescriptor &type = *GetTestType(catalog);
    TestComponent object;
    const ReflectionObjectRef object_ref = ReflectionObjectRef::ForMutable(type.id, &object);

    const ReflectionPropertyDescriptor &value = *GetProperty(type, "value");
    const ReflectionReadResult read = access.Read(object_ref, value.id);
    ASSERT_TRUE(read);
    ASSERT_NE(read.value.TryGet<int64_t>(), nullptr);
    EXPECT_EQ(*read.value.TryGet<int64_t>(), 7);

    ASSERT_TRUE(access.Write(object_ref, value.id, ReflectionValue{42}));
    EXPECT_EQ(object.value, 42);

    const ReflectionPropertyDescriptor &display_label = *GetProperty(type, "display_label");
    EXPECT_FALSE(HasFlag(display_label.flags, ReflectionPropertyFlags::Writable));
    const ReflectionReadResult label_read = access.Read(object_ref, display_label.id);
    ASSERT_TRUE(label_read);
    ASSERT_NE(label_read.value.TryGet<std::string>(), nullptr);
    EXPECT_EQ(*label_read.value.TryGet<std::string>(), "initial");
    EXPECT_EQ(access.Write(object_ref, display_label.id, ReflectionValue{"changed"}).status,
              ReflectionResultStatus::ReadOnly);

    const ReflectionPropertyDescriptor &guarded = *GetProperty(type, "guarded");
    EXPECT_EQ(access.Write(object_ref, guarded.id, ReflectionValue{-1}).status,
              ReflectionResultStatus::SetterRejected);
    EXPECT_EQ(object.guarded, 1);
    ASSERT_TRUE(access.Write(object_ref, guarded.id, ReflectionValue{11}));
    EXPECT_EQ(object.guarded, 11);

    EXPECT_EQ(access.Write(object_ref, value.id, ReflectionValue{"not an integer"}).status,
              ReflectionResultStatus::TypeMismatch);
    EXPECT_EQ(access.Read(object_ref, ReflectionPropertyId{0}).status,
              ReflectionResultStatus::UnknownProperty);

    const ReflectionObjectRef const_ref = ReflectionObjectRef::ForConst(type.id, &object);
    EXPECT_EQ(access.Write(const_ref, value.id, ReflectionValue{13}).status,
              ReflectionResultStatus::ReadOnly);

    OtherComponent other;
    const ReflectionObjectRef wrong_type = ReflectionObjectRef::ForMutable(type.id, &other);
    EXPECT_EQ(access.Read(wrong_type, value.id).status, ReflectionResultStatus::WrongObjectType);
    const ReflectionObjectRef unknown_type =
        ReflectionObjectRef::ForMutable(ReflectionTypeId{0xabcdef01u}, &object);
    EXPECT_EQ(access.Read(unknown_type, value.id).status, ReflectionResultStatus::UnknownType);
    system.Shutdown();
}

TEST(ReflectionRegistry, RejectsNumericBoundaryConversions)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({RegisterNumericComponent}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();
    const ReflectionTypeDescriptor &type = *catalog.FindType("tests::NumericComponent");
    NumericComponent object;
    const ReflectionObjectRef object_ref = ReflectionObjectRef::ForMutable(type.id, &object);

    EXPECT_EQ(access.Write(object_ref,
                           GetProperty(type, "signed_value")->id,
                           ReflectionValue{std::ldexp(1.0, 63)})
                  .status,
              ReflectionResultStatus::TypeMismatch);
    EXPECT_EQ(access.Write(object_ref,
                           GetProperty(type, "unsigned_value")->id,
                           ReflectionValue{std::ldexp(1.0, 64)})
                  .status,
              ReflectionResultStatus::TypeMismatch);
    EXPECT_EQ(access.Write(object_ref,
                           GetProperty(type, "ratio")->id,
                           ReflectionValue{static_cast<double>(std::numeric_limits<float>::max()) * 2.0})
                  .status,
              ReflectionResultStatus::TypeMismatch);
    EXPECT_EQ(access.Write(object_ref,
                           GetProperty(type, "ratio")->id,
                           ReflectionValue{std::numeric_limits<double>::infinity()})
                  .status,
              ReflectionResultStatus::TypeMismatch);
    system.Shutdown();
}

TEST(ReflectionRegistry, SetterAndCatalogRejectAccessAfterFreeze)
{
    EnttReflectionRegistry registry;
    EnttReflectionRegistrar registrar{registry};
    auto type = registrar.Type<TestComponent>("tests::TestComponent");
    ASSERT_TRUE(type);
    ASSERT_TRUE(type.Property<&TestComponent::value>("value"));
    auto duplicate_type = registrar.Type<TestComponent>("tests::OtherName");
    EXPECT_EQ(duplicate_type.GetResult().status, ReflectionResultStatus::DuplicateName);
    auto duplicate_name = registrar.Type<OtherComponent>("tests::TestComponent");
    EXPECT_EQ(duplicate_name.GetResult().status, ReflectionResultStatus::DuplicateName);
    auto duplicate_property_type = registrar.Type<OtherComponent>("tests::DuplicateProperties");
    ASSERT_TRUE(duplicate_property_type);
    ASSERT_TRUE(duplicate_property_type.Property<&OtherComponent::value>("value"));
    EXPECT_EQ(duplicate_property_type.Property<&OtherComponent::value>("value").status,
              ReflectionResultStatus::DuplicateName);
    ASSERT_TRUE(registry.Freeze());

    EXPECT_EQ(type.Property<&TestComponent::label>("late").status,
              ReflectionResultStatus::Frozen);
    auto late_type = registrar.Type<OtherComponent>("tests::OtherComponent");
    EXPECT_EQ(late_type.GetResult().status, ReflectionResultStatus::Frozen);
    EXPECT_EQ(registry.FindType("tests::OtherComponent"), nullptr);

    registry.Shutdown();
    registry.Shutdown();
    EXPECT_EQ(registry.GetState(), EnttReflectionRegistry::State::ShutDown);
}

TEST(ReflectionRegistry, RollsBackFailedInitializationAndRejectsCollisions)
{
    ReflectionSystem system;
    const ReflectionResult failed = system.Initialize({
        [](EnttReflectionRegistrar &registrar) {
            auto type = registrar.Type<TestComponent>("tests::Transient");
            if (!type)
            {
                return type.GetResult();
            }
            return ReflectionResult{ReflectionResultStatus::InvalidDescriptor, "forced failure"};
        }});
    EXPECT_EQ(failed.status, ReflectionResultStatus::InvalidDescriptor);
    EXPECT_EQ(system.GetCatalog(), nullptr);
    EXPECT_EQ(system.GetState(), ReflectionSystem::State::Constructed);
    ASSERT_TRUE(system.Initialize({RegisterTestComponent}));
    system.Shutdown();

    EnttReflectionRegistry collision_registry([](std::string_view) { return 0x12345678u; });
    EnttReflectionRegistrar registrar{collision_registry};
    auto first = registrar.Type<TestComponent>("tests::First");
    ASSERT_TRUE(first);
    auto second = registrar.Type<OtherComponent>("tests::Second");
    EXPECT_EQ(second.GetResult().status, ReflectionResultStatus::IdCollision);

    EnttReflectionRegistry property_collision_registry(
        [](std::string_view) { return 0x76543210u; });
    EnttReflectionRegistrar property_registrar{property_collision_registry};
    auto property_type = property_registrar.Type<TestComponent>("tests::Properties");
    ASSERT_TRUE(property_type);
    ASSERT_TRUE(property_type.Property<&TestComponent::value>("first"));
    EXPECT_EQ(property_type.Property<&TestComponent::label>("second").status,
              ReflectionResultStatus::IdCollision);
}

TEST(ReflectionRegistry, EnforcesOwnerThreadForLiveObjectAccess)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({RegisterTestComponent}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();
    const ReflectionTypeDescriptor &type = *GetTestType(catalog);
    const ReflectionPropertyDescriptor &value = *GetProperty(type, "value");
    TestComponent object;
    const ReflectionObjectRef object_ref = ReflectionObjectRef::ForMutable(type.id, &object);
    ReflectionResultStatus status = ReflectionResultStatus::Success;
    std::thread worker([&] { status = access.Read(object_ref, value.id).status; });
    worker.join();
    EXPECT_EQ(status, ReflectionResultStatus::InvalidObject);
    system.Shutdown();
}
