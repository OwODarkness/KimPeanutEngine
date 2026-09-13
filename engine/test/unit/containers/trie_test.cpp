#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "containers/trie.h"

namespace
{
    using kpengine::containers::Trie;
}

TEST(TrieTest, InsertsFindsAndRejectsDuplicateKeys)
{
    Trie<int> trie;

    EXPECT_TRUE(trie.Empty());
    EXPECT_TRUE(trie.Insert("render", 1));
    EXPECT_FALSE(trie.Empty());
    EXPECT_EQ(trie.Size(), 1U);
    ASSERT_NE(trie.Find("render"), nullptr);
    EXPECT_EQ(*trie.Find("render"), 1);
    EXPECT_TRUE(trie.Contains("render"));
    EXPECT_FALSE(trie.Contains("re"));
    EXPECT_FALSE(trie.Insert("render", 2));
    EXPECT_EQ(*trie.Find("render"), 1);
}

TEST(TrieTest, VisitsPrefixMatchesInLexicographicalOrder)
{
    Trie<int> trie;
    ASSERT_TRUE(trie.Insert("help", 1));
    ASSERT_TRUE(trie.Insert("hello", 2));
    ASSERT_TRUE(trie.Insert("render", 3));
    ASSERT_TRUE(trie.Insert("re", 4));

    std::vector<std::pair<std::string, int>> matches;
    trie.VisitPrefix("he", [&matches](std::string_view key, const int& value)
    {
        matches.emplace_back(key, value);
    });

    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0], (std::pair<std::string, int>{"hello", 2}));
    EXPECT_EQ(matches[1], (std::pair<std::string, int>{"help", 1}));

    int visited = 0;
    trie.VisitPrefix("re", [&visited](std::string_view, const int&)
    {
        ++visited;
        return visited < 2;
    });
    EXPECT_EQ(visited, 2);
}

TEST(TrieTest, ErasesKeysWithoutRemovingSharedPrefixes)
{
    Trie<std::string> trie;
    ASSERT_TRUE(trie.TryEmplace("read", "read command"));
    ASSERT_TRUE(trie.Insert("ready", "ready command"));
    ASSERT_TRUE(trie.Insert("reset", "reset command"));

    EXPECT_TRUE(trie.Erase("read"));
    EXPECT_FALSE(trie.Contains("read"));
    EXPECT_TRUE(trie.Contains("ready"));
    EXPECT_TRUE(trie.Contains("reset"));
    EXPECT_FALSE(trie.Erase("missing"));

    trie.InsertOrAssign("ready", "updated command");
    ASSERT_NE(trie.Find("ready"), nullptr);
    EXPECT_EQ(*trie.Find("ready"), "updated command");

    trie.Clear();
    EXPECT_TRUE(trie.Empty());
    EXPECT_EQ(trie.Size(), 0U);
}
