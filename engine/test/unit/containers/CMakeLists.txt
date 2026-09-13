add_executable(TrieTest
    trie_test.cpp
)

target_link_libraries(TrieTest PRIVATE Core KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(TrieTest)
