#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "command/command_parser.h"
#include "command/command_registry.h"

namespace
{
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandDesc;
    using kpengine::runtime::command::CommandParser;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandStatus;
    using kpengine::runtime::command::CommandValueType;

    CommandDesc MakeParseDescriptor()
    {
        return {
            "test.parse",
            "ParserTest",
            "Parse typed arguments",
            CommandCategory::Test,
            {},
            {{
                 {"enabled", CommandValueType::Boolean, true, {}, {}},
                 {"count", CommandValueType::SignedInteger, true, {}, {}},
                 {"ratio", CommandValueType::Float, true, {}, {}},
                 {"label", CommandValueType::String, true, {}, {}},
                 {"mode", CommandValueType::Enum, true, {}, {"fast", "safe"}},
                 {"limit", CommandValueType::UnsignedInteger, false, uint64_t{4}, {}},
             }},
            [](const CommandCall &, const kpengine::runtime::command::CommandContext &)
            { return kpengine::runtime::command::CommandResult{CommandStatus::Success, {}, 0, {}}; },
        };
    }
}

TEST(CommandParserTest, ParsesTypedNamedAndPositionalArguments)
{
    const CommandDesc descriptor = MakeParseDescriptor();
    const auto parsed = CommandParser::Parse(
        R"(test.parse true -7 1.25 "hello world" safe)", descriptor);

    ASSERT_TRUE(parsed.IsSuccess()) << parsed.diagnostic;
    ASSERT_TRUE(parsed.call.has_value());
    EXPECT_EQ(std::get<bool>(parsed.call->arguments.at("enabled")), true);
    EXPECT_EQ(std::get<int64_t>(parsed.call->arguments.at("count")), -7);
    EXPECT_DOUBLE_EQ(std::get<double>(parsed.call->arguments.at("ratio")), 1.25);
    EXPECT_EQ(std::get<std::string>(parsed.call->arguments.at("label")), "hello world");
    EXPECT_EQ(std::get<std::string>(parsed.call->arguments.at("mode")), "safe");
    EXPECT_EQ(std::get<uint64_t>(parsed.call->arguments.at("limit")), 4U);
}

TEST(CommandParserTest, SupportsEscapingAndNamedDefaults)
{
    const CommandDesc descriptor = MakeParseDescriptor();
    const auto parsed = CommandParser::Parse(
        R"(test.parse enabled=false count=12 ratio=0.5 label="quote: \"ok\"" mode=fast)",
        descriptor);

    ASSERT_TRUE(parsed.IsSuccess()) << parsed.diagnostic;
    EXPECT_EQ(std::get<bool>(parsed.call->arguments.at("enabled")), false);
    EXPECT_EQ(std::get<int64_t>(parsed.call->arguments.at("count")), 12);
    EXPECT_EQ(std::get<std::string>(parsed.call->arguments.at("label")), "quote: \"ok\"");
}

TEST(CommandParserTest, RejectsMalformedInputBeforeExecution)
{
    const CommandDesc descriptor = MakeParseDescriptor();

    const auto unterminated = CommandParser::Parse("test.parse \"unterminated", descriptor);
    EXPECT_FALSE(unterminated.IsSuccess());
    EXPECT_EQ(unterminated.diagnostic, "unterminated quoted argument");

    const auto invalid_enum = CommandParser::Parse(
        "test.parse true 1 1.0 label unknown", descriptor);
    EXPECT_FALSE(invalid_enum.IsSuccess());
    EXPECT_EQ(invalid_enum.diagnostic, "argument 'mode': invalid enum value 'unknown'");
}

TEST(CommandParserTest, StructuredCallsAreValidatedAndDefaultsAreApplied)
{
    CommandRegistry registry;
    int execution_count = 0;
    const auto registration = registry.Register({
        "test.structured",
        "ParserTest",
        "Validate structured arguments",
        CommandCategory::Test,
        {},
        {{{"count", CommandValueType::SignedInteger, true, {}, {}},
          {"label", CommandValueType::String, false, std::string{"default"}, {}}}},
        [&execution_count](const CommandCall &call,
                           const kpengine::runtime::command::CommandContext &)
        {
            ++execution_count;
            EXPECT_EQ(std::get<int64_t>(call.arguments.at("count")), 8);
            EXPECT_EQ(std::get<std::string>(call.arguments.at("label")), "default");
            return kpengine::runtime::command::CommandResult{CommandStatus::Success, {}, 0, {}};
        },
    });
    ASSERT_TRUE(registration.IsSuccess());

    const auto invalid = registry.Execute(
        {"test.structured", {{"count", std::string{"8"}}}}, {});
    EXPECT_EQ(invalid.status, CommandStatus::InvalidArguments);
    EXPECT_EQ(execution_count, 0);

    const auto valid = registry.Execute(
        {"test.structured", {{"count", int64_t{8}}}}, {});
    EXPECT_EQ(valid.status, CommandStatus::Success);
    EXPECT_EQ(execution_count, 1);

    const auto text = registry.ExecuteText("test.structured count=8", {});
    EXPECT_EQ(text.status, CommandStatus::Success);
    EXPECT_EQ(execution_count, 2);
}

TEST(CommandParserTest, ReportsInvalidSchemasAtRegistration)
{
    CommandRegistry registry;
    const auto result = registry.Register({
        "test.invalid_schema",
        "ParserTest",
        "",
        CommandCategory::Test,
        {},
        {{{"duplicate", CommandValueType::String, false, {}, {}},
          {"duplicate", CommandValueType::String, false, {}, {}}}},
        [](const CommandCall &, const kpengine::runtime::command::CommandContext &)
        { return kpengine::runtime::command::CommandResult{CommandStatus::Success, {}, 0, {}}; },
    });

    EXPECT_EQ(result.status,
              kpengine::runtime::command::CommandRegistrationStatus::InvalidSchema);
    EXPECT_NE(result.diagnostic.find("duplicate argument name"), std::string::npos);
}

TEST(CommandParserTest, CompletesCommandsArgumentsAndEnumValuesDeterministically)
{
    const CommandDesc descriptor = MakeParseDescriptor();
    const std::vector<CommandDesc> descriptors{
        descriptor,
        {"test.other", "ParserTest", "", CommandCategory::Test, {}, {},
         [](const CommandCall &, const kpengine::runtime::command::CommandContext &)
         { return kpengine::runtime::command::CommandResult{CommandStatus::Success, {}, 0, {}}; }},
    };

    EXPECT_EQ(CommandParser::Complete("test.", descriptors),
              (std::vector<std::string>{"test.other", "test.parse"}));
    EXPECT_EQ(CommandParser::Complete("test.parse mode=", descriptors),
              (std::vector<std::string>{"mode=fast", "mode=safe"}));
    EXPECT_EQ(CommandParser::Complete("test.parse ", descriptors),
              (std::vector<std::string>{"count=", "enabled=", "label=", "limit=", "mode=",
                                        "ratio="}));
}
