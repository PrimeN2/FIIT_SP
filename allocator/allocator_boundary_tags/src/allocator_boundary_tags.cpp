#include "../include/allocator_boundary_tags.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
    using fit_mode = allocator_with_fit_mode::fit_mode;

    struct block_header
    {
        size_t size_and_flags;
        block_header* prev_phys;
        block_header* next_phys;
        block_header* next_free;
    };

    struct allocator_state
    {
        std::pmr::memory_resource* parent_allocator;
        fit_mode mode;
        size_t managed_size;
        std::mutex mutex;
        block_header* free_head;
    };

    constexpr size_t occupied_mask = 1;
    constexpr size_t alignment = alignof(block_header);

    size_t align_up(size_t value, size_t align)
    {
        return (value + align - 1) / align * align;
    }

    char* as_bytes(void* ptr)
    {
        return static_cast<char*>(ptr);
    }

    const char* as_bytes(const void* ptr)
    {
        return static_cast<const char*>(ptr);
    }

    allocator_state* state_of(void* trusted_memory)
    {
        return static_cast<allocator_state*>(trusted_memory);
    }

    const allocator_state* state_of(const void* trusted_memory)
    {
        return static_cast<const allocator_state*>(trusted_memory);
    }

    char* blocks_begin(void* trusted_memory)
    {
        auto raw = reinterpret_cast<std::uintptr_t>(as_bytes(trusted_memory) + sizeof(allocator_state));
        raw = align_up(raw, alignment);
        return reinterpret_cast<char*>(raw);
    }

    const char* blocks_begin(const void* trusted_memory)
    {
        auto raw = reinterpret_cast<std::uintptr_t>(as_bytes(trusted_memory) + sizeof(allocator_state));
        raw = align_up(raw, alignment);
        return reinterpret_cast<const char*>(raw);
    }

    char* blocks_end(void* trusted_memory)
    {
        return blocks_begin(trusted_memory) + state_of(trusted_memory)->managed_size;
    }

    const char* blocks_end(const void* trusted_memory)
    {
        return blocks_begin(trusted_memory) + state_of(trusted_memory)->managed_size;
    }

    block_header* first_block(void* trusted_memory)
    {
        return state_of(trusted_memory)->managed_size < sizeof(block_header)
            ? nullptr
            : reinterpret_cast<block_header*>(blocks_begin(trusted_memory));
    }

    const block_header* first_block(const void* trusted_memory)
    {
        return state_of(trusted_memory)->managed_size < sizeof(block_header)
            ? nullptr
            : reinterpret_cast<const block_header*>(blocks_begin(trusted_memory));
    }

    size_t block_size(const block_header* block) noexcept
    {
        return block->size_and_flags & ~occupied_mask;
    }

    bool is_occupied(const block_header* block) noexcept
    {
        return (block->size_and_flags & occupied_mask) != 0;
    }

    void set_block_size(block_header* block, size_t size) noexcept
    {
        block->size_and_flags = size | (block->size_and_flags & occupied_mask);
    }

    void set_occupied(block_header* block, bool occupied) noexcept
    {
        block->size_and_flags = block_size(block) | (occupied ? occupied_mask : 0U);
    }

    void initialize_block(
        block_header* block,
        size_t size,
        bool occupied,
        block_header* prev_phys,
        block_header* next_phys,
        block_header* next_free = nullptr) noexcept
    {
        block->size_and_flags = size | (occupied ? occupied_mask : 0U);
        block->prev_phys = prev_phys;
        block->next_phys = next_phys;
        block->next_free = next_free;
    }

    void* payload_of(block_header* block) noexcept
    {
        return reinterpret_cast<char*>(block) + sizeof(block_header);
    }

    const void* payload_of(const block_header* block) noexcept
    {
        return reinterpret_cast<const char*>(block) + sizeof(block_header);
    }

    block_header* adjusted_pointer(block_header* ptr, std::ptrdiff_t offset) noexcept
    {
        return ptr == nullptr ? nullptr : reinterpret_cast<block_header*>(reinterpret_cast<char*>(ptr) + offset);
    }

    const block_header* adjusted_pointer(const block_header* ptr, std::ptrdiff_t offset) noexcept
    {
        return ptr == nullptr ? nullptr : reinterpret_cast<const block_header*>(reinterpret_cast<const char*>(ptr) + offset);
    }

    bool belongs_to_allocator(const void* trusted_memory, const void* ptr) noexcept
    {
        return ptr >= blocks_begin(trusted_memory) && ptr < blocks_end(trusted_memory);
    }

    block_header* last_block(void* trusted_memory) noexcept
    {
        auto* current = first_block(trusted_memory);
        while (current != nullptr && current->next_phys != nullptr)
        {
            current = current->next_phys;
        }
        return current;
    }

    const block_header* last_block(const void* trusted_memory) noexcept
    {
        auto* current = first_block(trusted_memory);
        while (current != nullptr && current->next_phys != nullptr)
        {
            current = current->next_phys;
        }
        return current;
    }

    void relink_free_block(
        allocator_state* state,
        block_header* block,
        block_header* prev_free,
        block_header* next_free) noexcept
    {
        block->next_free = next_free;
        if (prev_free == nullptr)
        {
            state->free_head = block;
        }
        else
        {
            prev_free->next_free = block;
        }
    }

    void remove_from_free_list(allocator_state* state, block_header* target, block_header* prev_free) noexcept
    {
        if (prev_free == nullptr)
        {
            state->free_head = target->next_free;
        }
        else
        {
            prev_free->next_free = target->next_free;
        }
        target->next_free = nullptr;
    }
}

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (_trusted_memory == nullptr)
    {
        return;
    }

    auto* state = state_of(_trusted_memory);
    auto* parent_allocator = state->parent_allocator;
    auto total_size = static_cast<size_t>(blocks_begin(_trusted_memory) - as_bytes(_trusted_memory)) + state->managed_size;

    state->mutex.~mutex();
    parent_allocator->deallocate(_trusted_memory, total_size, alignment);
    _trusted_memory = nullptr;
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags&& other) noexcept
    : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_boundary_tags& allocator_boundary_tags::operator=(
    allocator_boundary_tags&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    this->~allocator_boundary_tags();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;

    return *this;
}

allocator_boundary_tags::allocator_boundary_tags(
    size_t space_size,
    std::pmr::memory_resource* parent_allocator,
    allocator_with_fit_mode::fit_mode allocate_fit_mode)
    : _trusted_memory(nullptr)
{
    auto* allocator = parent_allocator == nullptr ? std::pmr::get_default_resource() : parent_allocator;
    auto header_offset = align_up(sizeof(allocator_state), alignment);
    auto total_size = header_offset + space_size;

    _trusted_memory = allocator->allocate(total_size, alignment);
    auto* state = ::new (_trusted_memory) allocator_state{
        allocator,
        allocate_fit_mode,
        space_size,
        std::mutex(),
        nullptr
    };

    try
    {
        if (space_size >= sizeof(block_header))
        {
            auto* block = first_block(_trusted_memory);
            initialize_block(block, space_size, false, nullptr, nullptr);
            state->free_head = block;
        }
    }
    catch (...)
    {
        state->mutex.~mutex();
        allocator->deallocate(_trusted_memory, total_size, alignment);
        _trusted_memory = nullptr;
        throw;
    }
}

[[nodiscard]] void* allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);

    block_header* selected = nullptr;
    block_header* selected_prev = nullptr;
    auto required_block_size = sizeof(block_header) + size;

    for (block_header *prev = nullptr, *current = state->free_head; current != nullptr; prev = current, current = current->next_free)
    {
        if (block_size(current) < required_block_size)
        {
            continue;
        }

        if (selected == nullptr)
        {
            selected = current;
            selected_prev = prev;
            if (state->mode == fit_mode::first_fit)
            {
                break;
            }
            continue;
        }

        if (state->mode == fit_mode::the_best_fit && block_size(current) < block_size(selected))
        {
            selected = current;
            selected_prev = prev;
        }

        if (state->mode == fit_mode::the_worst_fit && block_size(current) > block_size(selected))
        {
            selected = current;
            selected_prev = prev;
        }
    }

    if (selected == nullptr)
    {
        throw std::bad_alloc();
    }

    auto selected_size = block_size(selected);
    auto remainder = selected_size - required_block_size;

    if (remainder >= sizeof(block_header))
    {
        auto* split_block = reinterpret_cast<block_header*>(reinterpret_cast<char*>(selected) + required_block_size);
        initialize_block(split_block, remainder, false, selected, selected->next_phys, selected->next_free);

        if (split_block->next_phys != nullptr)
        {
            split_block->next_phys->prev_phys = split_block;
        }

        if (selected_prev == nullptr)
        {
            state->free_head = split_block;
        }
        else
        {
            selected_prev->next_free = split_block;
        }

        selected->next_phys = split_block;
        selected->next_free = nullptr;
        set_block_size(selected, required_block_size);
        set_occupied(selected, true);
    }
    else
    {
        remove_from_free_list(state, selected, selected_prev);
        set_occupied(selected, true);
    }

    return payload_of(selected);
}

void allocator_boundary_tags::do_deallocate_sm(
    void* at)
{
    if (at == nullptr)
    {
        return;
    }

    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);

    block_header* target = nullptr;
    for (auto* current = first_block(_trusted_memory); current != nullptr; current = current->next_phys)
    {
        if (payload_of(current) == at)
        {
            target = current;
            break;
        }
    }

    if (target == nullptr || !belongs_to_allocator(_trusted_memory, target) || !is_occupied(target))
    {
        throw std::invalid_argument("pointer does not belong to allocator_boundary_tags");
    }

    set_occupied(target, false);

    block_header* prev_free = nullptr;
    block_header* next_free = state->free_head;
    while (next_free != nullptr && next_free < target)
    {
        prev_free = next_free;
        next_free = next_free->next_free;
    }
    relink_free_block(state, target, prev_free, next_free);

    if (target->next_phys != nullptr && !is_occupied(target->next_phys))
    {
        auto* next_block = target->next_phys;
        target->next_free = next_block->next_free;
        set_block_size(target, block_size(target) + block_size(next_block));
        target->next_phys = next_block->next_phys;
        if (target->next_phys != nullptr)
        {
            target->next_phys->prev_phys = target;
        }
    }

    if (target->prev_phys != nullptr && !is_occupied(target->prev_phys))
    {
        auto* previous = target->prev_phys;
        previous->next_free = target->next_free;
        set_block_size(previous, block_size(previous) + block_size(target));
        previous->next_phys = target->next_phys;
        if (previous->next_phys != nullptr)
        {
            previous->next_phys->prev_phys = previous;
        }
    }
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);
    state->mode = mode;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    boundary_iterator it(_trusted_memory);
    while (it.get_ptr() != nullptr)
    {
        ++it;
    }
    return it;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    for (auto it = begin(); it != end(); ++it)
    {
        result.push_back({it.size(), it.occupied()});
    }
    return result;
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags& other)
    : allocator_boundary_tags(
        state_of(other._trusted_memory)->managed_size,
        state_of(other._trusted_memory)->parent_allocator,
        state_of(other._trusted_memory)->mode)
{
    auto* other_state = state_of(other._trusted_memory);
    std::lock_guard guard(other_state->mutex);

    std::memcpy(
        blocks_begin(_trusted_memory),
        blocks_begin(other._trusted_memory),
        other_state->managed_size);

    auto offset = as_bytes(_trusted_memory) - as_bytes(other._trusted_memory);
    auto* this_state = state_of(_trusted_memory);
    this_state->free_head = adjusted_pointer(other_state->free_head, offset);

    for (auto* current = first_block(_trusted_memory); current != nullptr; current = current->next_phys)
    {
        current->prev_phys = adjusted_pointer(current->prev_phys, offset);
        current->next_phys = adjusted_pointer(current->next_phys, offset);
        current->next_free = adjusted_pointer(current->next_free, offset);
    }
}

allocator_boundary_tags& allocator_boundary_tags::operator=(const allocator_boundary_tags& other)
{
    if (this == &other)
    {
        return *this;
    }

    allocator_boundary_tags copy(other);
    *this = std::move(copy);
    return *this;
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
    const allocator_boundary_tags::boundary_iterator& other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr && _trusted_memory == other._trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
    const allocator_boundary_tags::boundary_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator& allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (_occupied_ptr == nullptr)
    {
        return *this;
    }

    auto* current = static_cast<block_header*>(_occupied_ptr);
    current = current->next_phys;
    _occupied_ptr = current;
    _occupied = current != nullptr && is_occupied(current);
    return *this;
}

allocator_boundary_tags::boundary_iterator& allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (_trusted_memory == nullptr)
    {
        return *this;
    }

    if (_occupied_ptr == nullptr)
    {
        auto* current = last_block(_trusted_memory);
        _occupied_ptr = const_cast<block_header*>(current);
        _occupied = current != nullptr && is_occupied(current);
        return *this;
    }

    auto* current = static_cast<block_header*>(_occupied_ptr)->prev_phys;
    _occupied_ptr = current;
    _occupied = current != nullptr && is_occupied(current);
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int)
{
    auto copy = *this;
    --(*this);
    return copy;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    return _occupied_ptr == nullptr ? 0U : block_size(static_cast<block_header*>(_occupied_ptr));
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    return _occupied_ptr == nullptr ? nullptr : payload_of(static_cast<block_header*>(_occupied_ptr));
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void* trusted)
    : _occupied_ptr(first_block(trusted)), _occupied(false), _trusted_memory(trusted)
{
    if (_occupied_ptr != nullptr)
    {
        _occupied = is_occupied(static_cast<block_header*>(_occupied_ptr));
    }
}

void* allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
