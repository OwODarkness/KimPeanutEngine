#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "log/logger.h"

namespace
{
    TEST(LoggerTest, Cxx20FormattingOwnsTheResult)
    {
        auto &logger = kpengine::program::Logger::GetLogger();
        const auto before = logger.GetSnapshot().size();

        std::string category = "LoggerUnitTest";
        std::string value = "owned after return";
        logger.LogFormat(category, kpengine::program::LogLevel::Info, __LINE__, __FILE__,
                         "count={}, value={}", 42, std::string_view(value));

        category.clear();
        value.clear();

        const auto logs = logger.GetSnapshot();
        ASSERT_GT(logs.size(), before);
        const auto &entry = logs.back();
        EXPECT_EQ(entry.name, "LoggerUnitTest");
        EXPECT_EQ(entry.message, "count=42, value=owned after return");
    }

    TEST(LoggerTest, LegacyFormattingKeepsExistingCallersWorking)
    {
        auto &logger = kpengine::program::Logger::GetLogger();
        const auto before = logger.GetSnapshot().size();
        const std::string long_value(600, 'x');

        logger.Log("LoggerLegacyUnitTest", kpengine::program::LogLevel::Info, __LINE__, __FILE__,
                   "count=%d, value=%s", 7, long_value.c_str());

        const auto logs = logger.GetSnapshot();
        ASSERT_GT(logs.size(), before);
        EXPECT_EQ(logs.back().name, "LoggerLegacyUnitTest");
        EXPECT_EQ(logs.back().message, "count=7, value=" + long_value);
    }
}
