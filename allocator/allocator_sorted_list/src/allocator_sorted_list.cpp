#include "../include/allocator_sorted_list.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
    using fit_mode = allocator_with_fit_mode::fit_mode;

    struct block_header
    {
        block_header* next_free;
        size_t payload_size;
    };

    struct allocator_state
    {
        std::pmr::memory_resource* parent_allocator;
        fit_mode mode;
        size_t total_size;
        std::mutex mutex;
        block_header* free_head;
    };

    constexpr size_t alignment = alignof(std::max_align_t);

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
        auto state = state_of(trusted_memory);
        return as_bytes(trusted_memory) + state->total_size;
    }

    const char* blocks_end(const void* trusted_memory)
    {
        auto state = state_of(trusted_memory);
        return as_bytes(trusted_memory) + state->total_size;
    }

    block_header* first_block(void* trusted_memory)
    {
        auto begin = blocks_begin(trusted_memory);
        auto end = blocks_end(trusted_memory);

        if (end - begin < static_cast<std::ptrdiff_t>(sizeof(block_header) + 1))
        {
            return nullptr;
        }

        return reinterpret_cast<block_header*>(begin);
    }

    const block_header* first_block(const void* trusted_memory)
    {
        auto begin = blocks_begin(trusted_memory);
        auto end = blocks_end(trusted_memory);

        if (end - begin < static_cast<std::ptrdiff_t>(sizeof(block_header) + 1))
        {
            return nullptr;
        }

        return reinterpret_cast<const block_header*>(begin);
    }

    void initialize_single_free_block(void* trusted_memory)
    {
        auto state = state_of(trusted_memory);
        auto block = first_block(trusted_memory);
        if (block == nullptr)
        {
            state->free_head = nullptr;
            return;
        }

        auto available = static_cast<size_t>(blocks_end(trusted_memory) - reinterpret_cast<char*>(block));
        block->next_free = nullptr;
        block->payload_size = available - sizeof(block_header);
        state->free_head = block;
    }

    bool block_within_allocator(const void* trusted_memory, const block_header* block)
    {
        if (block == nullptr)
        {
            return false;
        }

        auto begin = blocks_begin(trusted_memory);
        auto end = blocks_end(trusted_memory);
        auto ptr = reinterpret_cast<const char*>(block);

        return ptr >= begin && ptr + sizeof(block_header) <= end;
    }

    block_header* next_physical_block(void* trusted_memory, block_header* block)
    {
        if (block == nullptr)
        {
            return nullptr;
        }

        auto next = reinterpret_cast<char*>(block) + sizeof(block_header) + block->payload_size;
        return next >= blocks_end(trusted_memory) ? nullptr : reinterpret_cast<block_header*>(next);
    }

    const block_header* next_physical_block(const void* trusted_memory, const block_header* block)
    {
        if (block == nullptr)
        {
            return nullptr;
        }

        auto next = reinterpret_cast<const char*>(block) + sizeof(block_header) + block->payload_size;
        return next >= blocks_end(trusted_memory) ? nullptr : reinterpret_cast<const block_header*>(next);
    }

    void* block_payload(block_header* block)
    {
        return reinterpret_cast<char*>(block) + sizeof(block_header);
    }

    const void* block_payload(const block_header* block)
    {
        return reinterpret_cast<const char*>(block) + sizeof(block_header);
    }

    block_header* block_from_payload(void* payload)
    {
        return reinterpret_cast<block_header*>(static_cast<char*>(payload) - sizeof(block_header));
    }

    block_header* adjusted_pointer(block_header* ptr, std::ptrdiff_t offset)
    {
        return ptr == nullptr ? nullptr : reinterpret_cast<block_header*>(reinterpret_cast<char*>(ptr) + offset);
    }

    const block_header* adjusted_pointer(const block_header* ptr, std::ptrdiff_t offset)
    {
        return ptr == nullptr ? nullptr : reinterpret_cast<const block_header*>(reinterpret_cast<const char*>(ptr) + offset);
    }
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (_trusted_memory == nullptr)
    {
        return;
    }

    auto state = state_of(_trusted_memory);
    auto parent_allocator = state->parent_allocator;
    auto total_size = state->total_size;

    state->mutex.~mutex();
    parent_allocator->deallocate(_trusted_memory, total_size, alignment);
    _trusted_memory = nullptr;
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list&& other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_sorted_list& allocator_sorted_list::operator=(
    allocator_sorted_list&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    this->~allocator_sorted_list();
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;

    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource* parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
    : _trusted_memory(nullptr)
{
    auto* allocator = parent_allocator == nullptr ? std::pmr::get_default_resource() : parent_allocator;
    auto total_size = align_up(sizeof(allocator_state), alignment) + std::max(space_size, sizeof(block_header) + size_t{1});

    _trusted_memory = allocator->allocate(total_size, alignment);

    auto* state = ::new (_trusted_memory) allocator_state{
        allocator,
        allocate_fit_mode,
        total_size,
        std::mutex(),
        nullptr
    };

    try
    {
        initialize_single_free_block(_trusted_memory);
    }
    catch (...)
    {
        state->mutex.~mutex();
        allocator->deallocate(_trusted_memory, total_size, alignment);
        _trusted_memory = nullptr;
        throw;
    }
}

[[nodiscard]] void* allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    if (size == 0)
    {
        size = 1;
    }

    auto* state = state_of(_trusted_memory);
    auto requested_size = align_up(size, alignment);

    std::lock_guard guard(state->mutex);

    block_header* selected = nullptr;
    block_header* selected_prev = nullptr;

    for (block_header *prev = nullptr, *current = state->free_head; current != nullptr; prev = current, current = current->next_free)
    {
        if (current->payload_size < requested_size)
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

        if (state->mode == fit_mode::the_best_fit && current->payload_size < selected->payload_size)
        {
            selected = current;
            selected_prev = prev;
        }

        if (state->mode == fit_mode::the_worst_fit && current->payload_size > selected->payload_size)
        {
            selected = current;
            selected_prev = prev;
        }
    }

    if (selected == nullptr)
    {
        throw std::bad_alloc();
    }

    auto remainder = selected->payload_size - requested_size;
    if (remainder > sizeof(block_header))
    {
        auto* split_block = reinterpret_cast<block_header*>(reinterpret_cast<char*>(block_payload(selected)) + requested_size);
        split_block->payload_size = remainder - sizeof(block_header);
        split_block->next_free = selected->next_free;

        if (selected_prev == nullptr)
        {
            state->free_head = split_block;
        }
        else
        {
            selected_prev->next_free = split_block;
        }

        selected->payload_size = requested_size;
        selected->next_free = nullptr;
    }
    else
    {
        if (selected_prev == nullptr)
        {
            state->free_head = selected->next_free;
        }
        else
        {
            selected_prev->next_free = selected->next_free;
        }

        selected->next_free = nullptr;
    }

    return block_payload(selected);
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list& other)
    : allocator_sorted_list(
        state_of(other._trusted_memory)->total_size - align_up(sizeof(allocator_state), alignment),
        state_of(other._trusted_memory)->parent_allocator,
        state_of(other._trusted_memory)->mode)
{
    auto* other_state = state_of(other._trusted_memory);
    auto* this_state = state_of(_trusted_memory);

    std::lock_guard guard(other_state->mutex);

    std::memcpy(
        blocks_begin(_trusted_memory),
        blocks_begin(other._trusted_memory),
        static_cast<size_t>(blocks_end(other._trusted_memory) - blocks_begin(other._trusted_memory)));

    auto offset = as_bytes(_trusted_memory) - as_bytes(other._trusted_memory);
    this_state->free_head = adjusted_pointer(other_state->free_head, offset);

    for (auto* block = first_block(_trusted_memory); block != nullptr; block = next_physical_block(_trusted_memory, block))
    {
        auto* original_block = adjusted_pointer(block, -offset);
        if (block_within_allocator(other._trusted_memory, original_block) && this_state->free_head != nullptr)
        {
            for (auto* free_original = other_state->free_head, *free_copy = this_state->free_head;
                 free_original != nullptr && free_copy != nullptr;
                 free_original = free_original->next_free, free_copy = free_copy->next_free)
            {
                if (free_original == original_block)
                {
                    block->next_free = adjusted_pointer(original_block->next_free, offset);
                    break;
                }
            }
        }
    }
}

allocator_sorted_list& allocator_sorted_list::operator=(const allocator_sorted_list& other)
{
    if (this == &other)
    {
        return *this;
    }

    allocator_sorted_list copy(other);
    *this = std::move(copy);
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

void allocator_sorted_list::do_deallocate_sm(
    void* at)
{
    if (at == nullptr)
    {
        return;
    }

    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);

    block_header* target = nullptr;
    bool target_is_free = false;
    auto* free_cursor = state->free_head;

    for (auto* current = first_block(_trusted_memory); current != nullptr; current = next_physical_block(_trusted_memory, current))
    {
        if (free_cursor == current)
        {
            if (block_payload(current) == at)
            {
                target = current;
                target_is_free = true;
                break;
            }
            free_cursor = free_cursor->next_free;
        }

        if (block_payload(current) == at)
        {
            target = current;
            break;
        }
    }

    if (target == nullptr || target_is_free)
    {
        throw std::invalid_argument("pointer does not belong to allocator_sorted_list");
    }

    block_header* prev_free = nullptr;
    block_header* next_free = state->free_head;
    while (next_free != nullptr && next_free < target)
    {
        prev_free = next_free;
        next_free = next_free->next_free;
    }

    target->next_free = next_free;
    if (prev_free == nullptr)
    {
        state->free_head = target;
    }
    else
    {
        prev_free->next_free = target;
    }

    if (next_free != nullptr &&
        reinterpret_cast<char*>(block_payload(target)) + target->payload_size == reinterpret_cast<char*>(next_free))
    {
        target->payload_size += sizeof(block_header) + next_free->payload_size;
        target->next_free = next_free->next_free;
    }

    if (prev_free != nullptr &&
        reinterpret_cast<char*>(block_payload(prev_free)) + prev_free->payload_size == reinterpret_cast<char*>(target))
    {
        prev_free->payload_size += sizeof(block_header) + target->payload_size;
        prev_free->next_free = target->next_free;
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);
    state->mode = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    auto* state = state_of(_trusted_memory);
    std::lock_guard guard(state->mutex);
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;

    auto* free_cursor = state_of(_trusted_memory)->free_head;
    for (auto* current = first_block(_trusted_memory); current != nullptr; current = next_physical_block(_trusted_memory, current))
    {
        auto is_free = free_cursor == current;
        result.push_back({current->payload_size, !is_free});

        if (is_free)
        {
            free_cursor = free_cursor->next_free;
        }
    }

    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(state_of(_trusted_memory)->free_head);
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator();
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator();
}

bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator& other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator& allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr != nullptr)
    {
        _free_ptr = static_cast<block_header*>(_free_ptr)->next_free;
    }

    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    return _free_ptr == nullptr ? 0 : static_cast<block_header*>(_free_ptr)->payload_size;
}

void* allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr == nullptr ? nullptr : block_payload(static_cast<block_header*>(_free_ptr));
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() : _free_ptr(nullptr) {}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void* trusted) : _free_ptr(trusted) {}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator& other) const noexcept
{
    return _current_ptr == other._current_ptr && _trusted_memory == other._trusted_memory;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator& allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_current_ptr == nullptr)
    {
        return *this;
    }

    auto* current = static_cast<block_header*>(_current_ptr);
    if (_free_ptr == _current_ptr)
    {
        _free_ptr = current->next_free;
    }

    _current_ptr = next_physical_block(_trusted_memory, current);
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int)
{
    auto copy = *this;
    ++(*this);
    return copy;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    return _current_ptr == nullptr ? 0 : static_cast<block_header*>(_current_ptr)->payload_size;
}

void* allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return _current_ptr == nullptr ? nullptr : block_payload(static_cast<block_header*>(_current_ptr));
}

allocator_sorted_list::sorted_iterator::sorted_iterator()
    : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(nullptr) {}

allocator_sorted_list::sorted_iterator::sorted_iterator(void* trusted)
    : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(trusted)
{
    if (trusted == nullptr)
    {
        return;
    }

    _free_ptr = state_of(trusted)->free_head;
    _current_ptr = first_block(trusted);
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    return _current_ptr != nullptr && _current_ptr != _free_ptr;
}
