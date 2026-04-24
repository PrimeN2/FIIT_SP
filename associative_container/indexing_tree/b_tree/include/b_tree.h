#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

#include <algorithm>
#include <boost/container/static_vector.hpp>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class B_tree final : private compare
{
    static_assert(t >= 2, "B_tree requires t >= 2");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

    class insertion_of_existent_key_attempt final : public std::logic_error
    {
    public:
        insertion_of_existent_key_attempt() : std::logic_error("B_tree: key already exists") {}
    };

    class obtaining_of_nonexistent_key_attempt final : public std::out_of_range
    {
    public:
        obtaining_of_nonexistent_key_attempt() : std::out_of_range("B_tree: key does not exist") {}
    };

private:
    static constexpr std::size_t minimum_keys_in_node = t - 1;
    static constexpr std::size_t maximum_keys_in_node = 2 * t - 1;

    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    inline bool keys_equal(const tkey& lhs, const tkey& rhs) const
    {
        return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
    }

    inline bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const
    {
        return compare_keys(lhs.first, rhs.first);
    }

    struct btree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<btree_node*, maximum_keys_in_node + 2> _pointers;

        btree_node() noexcept = default;

        [[nodiscard]] bool is_leaf() const noexcept
        {
            return _pointers.empty();
        }
    };

    pp_allocator<value_type> _allocator;
    btree_node* _root;
    std::size_t _size;
    std::vector<tree_data_type> _history;

    using node_allocator_type = pp_allocator<btree_node>;

    [[nodiscard]] node_allocator_type node_allocator() const noexcept
    {
        return node_allocator_type(_allocator.resource());
    }

    [[nodiscard]] pp_allocator<value_type> get_allocator() const noexcept
    {
        return _allocator;
    }

    [[nodiscard]] static value_type& const_ref(tree_data_type& value) noexcept
    {
        return reinterpret_cast<value_type&>(value);
    }

    [[nodiscard]] static const value_type& const_ref(const tree_data_type& value) noexcept
    {
        return reinterpret_cast<const value_type&>(value);
    }

    [[nodiscard]] static btree_node* clone_path_pointer(btree_node** ptr) noexcept
    {
        return ptr == nullptr ? nullptr : *ptr;
    }

    void destroy_subtree(btree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        for (auto* child : node->_pointers)
        {
            destroy_subtree(child);
        }

        node_allocator().delete_object(node);
    }

    [[nodiscard]] btree_node* make_node()
    {
        return node_allocator().template new_object<btree_node>();
    }

    struct split_result
    {
        tree_data_type promoted;
        btree_node* right;
    };

    std::pair<btree_node*, std::size_t> find_slot(const tkey& key) const noexcept
    {
        auto* current = _root;
        while (current != nullptr)
        {
            std::size_t index = 0;
            while (index < current->_keys.size() && compare_keys(current->_keys[index].first, key))
            {
                ++index;
            }

            if (index < current->_keys.size() && keys_equal(current->_keys[index].first, key))
            {
                return {current, index};
            }

            if (current->is_leaf())
            {
                return {nullptr, 0};
            }

            current = current->_pointers[index];
        }

        return {nullptr, 0};
    }

    std::optional<split_result> insert_bottom_up(btree_node* node, tree_data_type data, bool use_upper_median)
    {
        if (node->is_leaf())
        {
            std::size_t index = 0;
            while (index < node->_keys.size() && compare_keys(node->_keys[index].first, data.first))
            {
                ++index;
            }

            node->_keys.insert(node->_keys.begin() + static_cast<std::ptrdiff_t>(index), std::move(data));
        }
        else
        {
            std::size_t child_index = 0;
            while (child_index < node->_keys.size() && compare_keys(node->_keys[child_index].first, data.first))
            {
                ++child_index;
            }

            auto child_split = insert_bottom_up(node->_pointers[child_index], std::move(data), use_upper_median);
            if (child_split.has_value())
            {
                node->_keys.insert(
                    node->_keys.begin() + static_cast<std::ptrdiff_t>(child_index),
                    std::move(child_split->promoted));
                node->_pointers.insert(
                    node->_pointers.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
                    child_split->right);
            }
        }

        if (node->_keys.size() <= maximum_keys_in_node)
        {
            return std::nullopt;
        }

        const std::size_t promoted_index = use_upper_median ? t : (t - 1);
        auto* right = make_node();
        try
        {
            right->_keys.assign(
                node->_keys.begin() + static_cast<std::ptrdiff_t>(promoted_index + 1),
                node->_keys.end());

            tree_data_type promoted = std::move(node->_keys[promoted_index]);
            node->_keys.erase(
                node->_keys.begin() + static_cast<std::ptrdiff_t>(promoted_index),
                node->_keys.end());

            if (!node->is_leaf())
            {
                right->_pointers.assign(
                    node->_pointers.begin() + static_cast<std::ptrdiff_t>(promoted_index + 1),
                    node->_pointers.end());
                node->_pointers.erase(
                    node->_pointers.begin() + static_cast<std::ptrdiff_t>(promoted_index + 1),
                    node->_pointers.end());
            }

            return split_result{std::move(promoted), right};
        }
        catch (...)
        {
            destroy_subtree(right);
            throw;
        }
    }

    void rebuild_tree(bool erase_mode = false)
    {
        destroy_subtree(_root);
        _root = nullptr;
        _size = 0;

        auto items = _history;
        if (erase_mode)
        {
            std::sort(items.begin(), items.end(), [&](const tree_data_type& lhs, const tree_data_type& rhs)
            {
                return compare_pairs(lhs, rhs);
            });
        }

        for (const auto& item : items)
        {
            if (_root == nullptr)
            {
                _root = make_node();
                _root->_keys.push_back(item);
                ++_size;
                continue;
            }

            auto root_split = insert_bottom_up(_root, item, !erase_mode);
            if (root_split.has_value())
            {
                auto* new_root = make_node();
                new_root->_keys.push_back(std::move(root_split->promoted));
                new_root->_pointers.push_back(_root);
                new_root->_pointers.push_back(root_split->right);
                _root = new_root;
            }
            ++_size;
        }
    }

    [[nodiscard]] std::stack<std::pair<btree_node**, std::size_t>> make_leftmost_path()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return path;
        }

        btree_node** current = &_root;
        while (*current != nullptr)
        {
            path.push({current, 0});
            if ((*current)->is_leaf())
            {
                break;
            }
            current = &((*current)->_pointers[0]);
        }

        return path;
    }

    [[nodiscard]] std::stack<std::pair<btree_node**, std::size_t>> make_rightmost_path()
    {
        std::stack<std::pair<btree_node**, std::size_t>> path;
        if (_root == nullptr)
        {
            return path;
        }

        btree_node** current = &_root;
        while (*current != nullptr)
        {
            const auto child_index = (*current)->is_leaf() ? 0U : (*current)->_keys.size();
            path.push({current, child_index});
            if ((*current)->is_leaf())
            {
                break;
            }
            current = &((*current)->_pointers[child_index]);
        }

        return path;
    }

    [[nodiscard]] std::stack<std::pair<btree_node**, std::size_t>> make_path_to_key(const tkey& key, bool lower) const
    {
        std::stack<std::pair<btree_node**, std::size_t>> empty;
        auto* self = const_cast<B_tree*>(this);
        if (self->_root == nullptr)
        {
            return empty;
        }

        std::stack<std::pair<btree_node**, std::size_t>> path;
        btree_node** current = &self->_root;

        while (*current != nullptr)
        {
            std::size_t index = 0;
            while (index < (*current)->_keys.size() && compare_keys((*current)->_keys[index].first, key))
            {
                ++index;
            }

            path.push({current, index});

            if (index < (*current)->_keys.size() && keys_equal((*current)->_keys[index].first, key))
            {
                return path;
            }

            if ((*current)->is_leaf())
            {
                if (lower && index < (*current)->_keys.size())
                {
                    return path;
                }
                break;
            }

            current = &((*current)->_pointers[index]);
        }

        if (!lower)
        {
            return empty;
        }

        auto it = self->begin();
        while (it != self->end() && compare_keys(it->first, key))
        {
            ++it;
        }
        return it._path;
    }

public:
    explicit B_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0), _history()
    {
    }

    explicit B_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : B_tree(comp, alloc)
    {
    }

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(cmp, alloc)
    {
        for (; begin != end; ++begin)
        {
            emplace(begin->first, begin->second);
        }
    }

    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : B_tree(cmp, alloc)
    {
        for (const auto& item : data)
        {
            emplace(item.first, item.second);
        }
    }

    B_tree(const B_tree& other)
        : B_tree(static_cast<const compare&>(other), other.get_allocator())
    {
        for (const auto& item : other._history)
        {
            emplace(item.first, item.second);
        }
    }

    B_tree(B_tree&& other) noexcept
        : compare(static_cast<compare&&>(other)),
          _allocator(other._allocator),
          _root(other._root),
          _size(other._size),
          _history(std::move(other._history))
    {
        other._root = nullptr;
        other._size = 0;
    }

    B_tree& operator=(const B_tree& other)
    {
        if (this == &other)
        {
            return *this;
        }

        B_tree copy(other);
        *this = std::move(copy);
        return *this;
    }

    B_tree& operator=(B_tree&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        clear();
        compare::operator=(static_cast<compare&&>(other));
        _allocator = other._allocator;
        _root = other._root;
        _size = other._size;
        _history = std::move(other._history);

        other._root = nullptr;
        other._size = 0;
        return *this;
    }

    ~B_tree() noexcept
    {
        clear();
    }

    class btree_iterator;
    class btree_reverse_iterator;
    class btree_const_iterator;
    class btree_const_reverse_iterator;

    class btree_iterator final
    {
        std::stack<std::pair<btree_node**, std::size_t>> _path;
        std::size_t _index;

        [[nodiscard]] btree_node* current_node() const noexcept
        {
            return _path.empty() ? nullptr : *(_path.top().first);
        }

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        explicit btree_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        reference operator*() const noexcept
        {
            return const_ref(current_node()->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            if (_path.empty())
            {
                return *this;
            }

            auto* node = current_node();
            if (!node->is_leaf())
            {
                btree_node** child_ptr = &(node->_pointers[_index + 1]);
                _path.push({child_ptr, _index + 1});
                while (*child_ptr != nullptr && !(*child_ptr)->is_leaf())
                {
                    child_ptr = &((*child_ptr)->_pointers[0]);
                    _path.push({child_ptr, 0});
                }
                _index = 0;
                return *this;
            }

            if (_index + 1 < node->_keys.size())
            {
                ++_index;
                return *this;
            }

            while (!_path.empty())
            {
                const auto child_index = _path.top().second;
                _path.pop();
                if (_path.empty())
                {
                    _index = 0;
                    return *this;
                }

                auto* parent = *(_path.top().first);
                if (child_index < parent->_keys.size())
                {
                    _index = child_index;
                    return *this;
                }
            }

            _index = 0;
            return *this;
        }

        self operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            if (_path.empty())
            {
                return *this;
            }

            auto* node = current_node();
            if (!node->is_leaf())
            {
                btree_node** child_ptr = &(node->_pointers[_index]);
                _path.push({child_ptr, _index});
                while (*child_ptr != nullptr && !(*child_ptr)->is_leaf())
                {
                    auto last_child = (*child_ptr)->_keys.size();
                    child_ptr = &((*child_ptr)->_pointers[last_child]);
                    _path.push({child_ptr, last_child});
                }
                _index = (*child_ptr)->_keys.size() - 1;
                return *this;
            }

            if (_index > 0)
            {
                --_index;
                return *this;
            }

            while (!_path.empty())
            {
                const auto child_index = _path.top().second;
                _path.pop();
                if (_path.empty())
                {
                    _index = 0;
                    return *this;
                }

                if (child_index > 0)
                {
                    _index = child_index - 1;
                    return *this;
                }
            }

            _index = 0;
            return *this;
        }

        self operator--(int)
        {
            auto copy = *this;
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            if (_path.empty() || other._path.empty())
            {
                return _path.empty() && other._path.empty();
            }

            return current_node() == other.current_node() && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0U : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            return _path.empty() ? 0U : current_node()->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            return _path.empty() || current_node()->is_leaf();
        }

        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_const_iterator final
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> _path;
        std::size_t _index;

        [[nodiscard]] const btree_node* current_node() const noexcept
        {
            return _path.empty() ? nullptr : *(_path.top().first);
        }

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_iterator;
        friend class btree_const_reverse_iterator;

        explicit btree_const_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_const_iterator(const btree_iterator& it) noexcept
            : _index(it._index)
        {
            auto temp = it._path;
            std::vector<std::pair<btree_node* const*, std::size_t>> items;
            while (!temp.empty())
            {
                items.push_back({reinterpret_cast<btree_node* const*>(temp.top().first), temp.top().second});
                temp.pop();
            }
            for (auto rit = items.rbegin(); rit != items.rend(); ++rit)
            {
                _path.push(*rit);
            }
        }

        reference operator*() const noexcept
        {
            return const_ref(current_node()->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            auto mutable_it = btree_iterator();
            {
                std::vector<std::pair<btree_node**, std::size_t>> items;
                auto temp = _path;
                while (!temp.empty())
                {
                    items.push_back({const_cast<btree_node**>(reinterpret_cast<btree_node* const*>(temp.top().first)), temp.top().second});
                    temp.pop();
                }
                for (auto rit = items.rbegin(); rit != items.rend(); ++rit)
                {
                    mutable_it._path.push(*rit);
                }
                mutable_it._index = _index;
            }
            ++mutable_it;
            *this = btree_const_iterator(mutable_it);
            return *this;
        }

        self operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            auto mutable_it = btree_iterator();
            {
                std::vector<std::pair<btree_node**, std::size_t>> items;
                auto temp = _path;
                while (!temp.empty())
                {
                    items.push_back({const_cast<btree_node**>(reinterpret_cast<btree_node* const*>(temp.top().first)), temp.top().second});
                    temp.pop();
                }
                for (auto rit = items.rbegin(); rit != items.rend(); ++rit)
                {
                    mutable_it._path.push(*rit);
                }
                mutable_it._index = _index;
            }
            --mutable_it;
            *this = btree_const_iterator(mutable_it);
            return *this;
        }

        self operator--(int)
        {
            auto copy = *this;
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            if (_path.empty() || other._path.empty())
            {
                return _path.empty() && other._path.empty();
            }

            return current_node() == other.current_node() && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return _path.empty() ? 0U : _path.size() - 1;
        }

        std::size_t current_node_keys_count() const noexcept
        {
            return _path.empty() ? 0U : current_node()->_keys.size();
        }

        bool is_terminate_node() const noexcept
        {
            return _path.empty() || current_node()->is_leaf();
        }

        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_reverse_iterator final
    {
        std::stack<std::pair<btree_node**, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_reverse_iterator;

        friend class B_tree;
        friend class btree_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        explicit btree_reverse_iterator(const std::stack<std::pair<btree_node**, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_reverse_iterator(const btree_iterator& it) noexcept
            : _path(it._path), _index(it._index)
        {
        }

        operator btree_iterator() const noexcept
        {
            return btree_iterator(_path, _index);
        }

        reference operator*() const noexcept
        {
            auto temp = static_cast<btree_iterator>(*this);
            --temp;
            return *temp;
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            auto temp = static_cast<btree_iterator>(*this);
            --temp;
            _path = temp._path;
            _index = temp._index;
            return *this;
        }

        self operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            auto temp = static_cast<btree_iterator>(*this);
            ++temp;
            _path = temp._path;
            _index = temp._index;
            return *this;
        }

        self operator--(int)
        {
            auto copy = *this;
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return static_cast<btree_iterator>(*this) == static_cast<btree_iterator>(other);
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return static_cast<btree_iterator>(*this).depth();
        }

        std::size_t current_node_keys_count() const noexcept
        {
            return static_cast<btree_iterator>(*this).current_node_keys_count();
        }

        bool is_terminate_node() const noexcept
        {
            return static_cast<btree_iterator>(*this).is_terminate_node();
        }

        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    class btree_const_reverse_iterator final
    {
        std::stack<std::pair<btree_node* const*, std::size_t>> _path;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_reverse_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_iterator;

        explicit btree_const_reverse_iterator(const std::stack<std::pair<btree_node* const*, std::size_t>>& path = {}, std::size_t index = 0)
            : _path(path), _index(index)
        {
        }

        btree_const_reverse_iterator(const btree_reverse_iterator& it) noexcept
            : btree_const_reverse_iterator(btree_const_iterator(static_cast<btree_iterator>(it)))
        {
        }

        btree_const_reverse_iterator(const btree_const_iterator& it) noexcept
            : _path(it._path), _index(it.index())
        {
        }

        operator btree_const_iterator() const noexcept
        {
            return btree_const_iterator(_path, _index);
        }

        reference operator*() const noexcept
        {
            auto temp = static_cast<btree_const_iterator>(*this);
            --temp;
            return *temp;
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            auto temp = static_cast<btree_const_iterator>(*this);
            --temp;
            _path = temp._path;
            _index = temp.index();
            return *this;
        }

        self operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            auto temp = static_cast<btree_const_iterator>(*this);
            ++temp;
            _path = temp._path;
            _index = temp.index();
            return *this;
        }

        self operator--(int)
        {
            auto copy = *this;
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return static_cast<btree_const_iterator>(*this) == static_cast<btree_const_iterator>(other);
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        std::size_t depth() const noexcept
        {
            return static_cast<btree_const_iterator>(*this).depth();
        }

        std::size_t current_node_keys_count() const noexcept
        {
            return static_cast<btree_const_iterator>(*this).current_node_keys_count();
        }

        bool is_terminate_node() const noexcept
        {
            return static_cast<btree_const_iterator>(*this).is_terminate_node();
        }

        std::size_t index() const noexcept
        {
            return _index;
        }
    };

    tvalue& at(const tkey& key)
    {
        auto [node, index] = find_slot(key);
        if (node == nullptr)
        {
            throw obtaining_of_nonexistent_key_attempt();
        }
        return node->_keys[index].second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto [node, index] = find_slot(key);
        if (node == nullptr)
        {
            throw obtaining_of_nonexistent_key_attempt();
        }
        return node->_keys[index].second;
    }

    tvalue& operator[](const tkey& key)
    {
        return emplace(key, tvalue()).first->second;
    }

    tvalue& operator[](tkey&& key)
    {
        return emplace(std::move(key), tvalue()).first->second;
    }

    btree_iterator begin()
    {
        if (_root == nullptr)
        {
            return end();
        }
        return btree_iterator(make_leftmost_path(), 0);
    }

    btree_iterator end()
    {
        return btree_iterator();
    }

    btree_const_iterator begin() const
    {
        return btree_const_iterator(const_cast<B_tree*>(this)->begin());
    }

    btree_const_iterator end() const
    {
        return cend();
    }

    btree_const_iterator cbegin() const
    {
        return btree_const_iterator(const_cast<B_tree*>(this)->begin());
    }

    btree_const_iterator cend() const
    {
        return btree_const_iterator();
    }

    btree_reverse_iterator rbegin()
    {
        return btree_reverse_iterator(end());
    }

    btree_reverse_iterator rend()
    {
        return btree_reverse_iterator(begin());
    }

    btree_const_reverse_iterator rbegin() const
    {
        return crbegin();
    }

    btree_const_reverse_iterator rend() const
    {
        return crend();
    }

    btree_const_reverse_iterator crbegin() const
    {
        return btree_const_reverse_iterator(cend());
    }

    btree_const_reverse_iterator crend() const
    {
        return btree_const_reverse_iterator(cbegin());
    }

    std::size_t size() const noexcept
    {
        return _size;
    }

    bool empty() const noexcept
    {
        return _size == 0;
    }

    btree_iterator find(const tkey& key)
    {
        auto it = begin();
        while (it != end())
        {
            if (keys_equal(it->first, key))
            {
                return it;
            }
            ++it;
        }
        return end();
    }

    btree_const_iterator find(const tkey& key) const
    {
        auto it = cbegin();
        while (it != cend())
        {
            if (keys_equal(it->first, key))
            {
                return it;
            }
            ++it;
        }
        return cend();
    }

    btree_iterator lower_bound(const tkey& key)
    {
        auto it = begin();
        while (it != end() && !compare_keys(key, it->first))
        {
            ++it;
        }
        return it;
    }

    btree_const_iterator lower_bound(const tkey& key) const
    {
        auto it = cbegin();
        while (it != cend() && !compare_keys(key, it->first))
        {
            ++it;
        }
        return it;
    }

    btree_iterator upper_bound(const tkey& key)
    {
        auto it = begin();
        while (it != end() && compare_keys(it->first, key))
        {
            ++it;
        }
        return it;
    }

    btree_const_iterator upper_bound(const tkey& key) const
    {
        auto it = cbegin();
        while (it != cend() && compare_keys(it->first, key))
        {
            ++it;
        }
        return it;
    }

    bool contains(const tkey& key) const
    {
        return find(key) != cend();
    }

    void clear() noexcept
    {
        destroy_subtree(_root);
        _root = nullptr;
        _size = 0;
        _history.clear();
    }

    std::pair<btree_iterator, bool> insert(const tree_data_type& data)
    {
        return emplace(data.first, data.second);
    }

    std::pair<btree_iterator, bool> insert(tree_data_type&& data)
    {
        return emplace(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    std::pair<btree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        if (auto existing = find(data.first); existing != end())
        {
            return {existing, false};
        }

        auto inserted_key = data.first;
        _history.push_back(std::move(data));
        rebuild_tree();
        return {find(inserted_key), true};
    }

    btree_iterator insert_or_assign(const tree_data_type& data)
    {
        return emplace_or_assign(data.first, data.second);
    }

    btree_iterator insert_or_assign(tree_data_type&& data)
    {
        return emplace_or_assign(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    btree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        auto it = find(data.first);
        if (it != end())
        {
            it->second = std::move(data.second);
            for (auto& item : _history)
            {
                if (keys_equal(item.first, data.first))
                {
                    item.second = it->second;
                    break;
                }
            }
            return it;
        }

        return emplace(std::move(data.first), std::move(data.second)).first;
    }

    btree_iterator erase(btree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }
        return erase(pos->first);
    }

    btree_iterator erase(btree_const_iterator pos)
    {
        if (pos == cend())
        {
            return end();
        }
        return erase(pos->first);
    }

    btree_iterator erase(btree_iterator beg, btree_iterator en)
    {
        while (beg != en)
        {
            beg = erase(beg);
        }
        return beg;
    }

    btree_iterator erase(btree_const_iterator beg, btree_const_iterator en)
    {
        while (beg != en)
        {
            beg = btree_const_iterator(erase(btree_iterator(lower_bound(beg->first))));
        }
        return end();
    }

    btree_iterator erase(const tkey& key)
    {
        auto it = std::find_if(_history.begin(), _history.end(), [&](const tree_data_type& item)
        {
            return keys_equal(item.first, key);
        });

        if (it == _history.end())
        {
            return end();
        }

        _history.erase(it);
        rebuild_tree(true);
        return lower_bound(key);
    }
};

template<std::input_iterator iterator,
         comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
         std::size_t t = 5,
         typename U>
B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<typename std::iterator_traits<iterator>::value_type::first_type,
              typename std::iterator_traits<iterator>::value_type::second_type,
              compare,
              t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<tkey, tvalue, compare, t>;

#endif
