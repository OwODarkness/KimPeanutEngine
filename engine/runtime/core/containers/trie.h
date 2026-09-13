#ifndef KPENGINE_RUNTIME_CORE_CONTAINERS_TRIE_H
#define KPENGINE_RUNTIME_CORE_CONTAINERS_TRIE_H

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kpengine::containers
{

template <typename T, typename CharT = char>
class Trie final
{
public:
    using value_type = T;
    using char_type = CharT;
    using key_type = std::basic_string<CharT>;
    using key_view = std::basic_string_view<CharT>;
    using size_type = std::size_t;

    Trie() = default;
    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;
    Trie(Trie&&) noexcept = default;
    Trie& operator=(Trie&&) noexcept = default;
    ~Trie() = default;

    /** Returns false without changing the trie when key already exists. */
    bool Insert(key_view key, T value)
    {
        Node* node = GetOrCreateNode(key);
        if (node->value.has_value())
        {
            return false;
        }

        node->value.emplace(std::move(value));
        ++size_;
        return true;
    }

    /** Inserts key or replaces its existing value. */
    void InsertOrAssign(key_view key, T value)
    {
        Node* node = GetOrCreateNode(key);
        if (!node->value.has_value())
        {
            ++size_;
        }
        node->value.emplace(std::move(value));
    }

    template <typename... Args>
    bool TryEmplace(key_view key, Args&&... args)
    {
        Node* node = GetOrCreateNode(key);
        if (node->value.has_value())
        {
            return false;
        }

        node->value.emplace(std::forward<Args>(args)...);
        ++size_;
        return true;
    }

    bool Erase(key_view key)
    {
        Node* node = &root_;
        std::vector<std::pair<Node*, CharT>> path;
        path.reserve(key.size());

        for (const CharT character : key)
        {
            const auto child = node->children.find(character);
            if (child == node->children.end())
            {
                return false;
            }

            path.emplace_back(node, character);
            node = child->second.get();
        }

        if (!node->value.has_value())
        {
            return false;
        }

        node->value.reset();
        --size_;

        for (auto pathEntry = path.rbegin(); pathEntry != path.rend(); ++pathEntry)
        {
            Node* parent = pathEntry->first;
            const CharT character = pathEntry->second;
            auto child = parent->children.find(character);
            if (child == parent->children.end() || child->second->value.has_value() ||
                !child->second->children.empty())
            {
                break;
            }
            parent->children.erase(child);
        }

        return true;
    }

    void Clear() noexcept
    {
        root_.children.clear();
        root_.value.reset();
        size_ = 0;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return size_ == 0;
    }

    [[nodiscard]] size_type Size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] T* Find(key_view key) noexcept
    {
        Node* node = FindNode(key);
        return node != nullptr && node->value.has_value() ? &node->value.value() : nullptr;
    }

    [[nodiscard]] const T* Find(key_view key) const noexcept
    {
        const Node* node = FindNode(key);
        return node != nullptr && node->value.has_value() ? &node->value.value() : nullptr;
    }

    [[nodiscard]] bool Contains(key_view key) const noexcept
    {
        return Find(key) != nullptr;
    }

    template <typename Visitor>
    void VisitPrefix(key_view prefix, Visitor&& visitor) const
    {
        const Node* node = FindNode(prefix);
        if (node == nullptr)
        {
            return;
        }

        key_type key(prefix);
        VisitNode(*node, key, visitor);
    }

private:
    struct Node
    {
        std::map<CharT, std::unique_ptr<Node>> children;
        std::optional<T> value;
    };

    Node* GetOrCreateNode(key_view key)
    {
        Node* node = &root_;
        for (const CharT character : key)
        {
            auto child = node->children.find(character);
            if (child == node->children.end())
            {
                auto newNode = std::make_unique<Node>();
                child = node->children.emplace(character, std::move(newNode)).first;
            }
            node = child->second.get();
        }
        return node;
    }

    [[nodiscard]] Node* FindNode(key_view key) noexcept
    {
        Node* node = &root_;
        for (const CharT character : key)
        {
            const auto child = node->children.find(character);
            if (child == node->children.end())
            {
                return nullptr;
            }
            node = child->second.get();
        }
        return node;
    }

    [[nodiscard]] const Node* FindNode(key_view key) const noexcept
    {
        const Node* node = &root_;
        for (const CharT character : key)
        {
            const auto child = node->children.find(character);
            if (child == node->children.end())
            {
                return nullptr;
            }
            node = child->second.get();
        }
        return node;
    }

    template <typename Visitor>
    static bool InvokeVisitor(Visitor& visitor, key_view key, const T& value)
    {
        using result_type = std::invoke_result_t<Visitor&, key_view, const T&>;
        if constexpr (std::is_same_v<result_type, bool>)
        {
            return std::invoke(visitor, key, value);
        }
        else
        {
            std::invoke(visitor, key, value);
            return true;
        }
    }

    template <typename Visitor>
    static bool VisitNode(const Node& node, key_type& key, Visitor& visitor)
    {
        if (node.value.has_value() && !InvokeVisitor(visitor, key, node.value.value()))
        {
            return false;
        }

        for (const auto& [character, child] : node.children)
        {
            key.push_back(character);
            if (!VisitNode(*child, key, visitor))
            {
                return false;
            }
            key.pop_back();
        }
        return true;
    }

    Node root_;
    size_type size_ = 0;
};

} // namespace kpengine::containers

#endif
