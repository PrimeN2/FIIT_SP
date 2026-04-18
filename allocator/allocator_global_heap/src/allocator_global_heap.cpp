#include "allocator_global_heap.h"
#include <mutex>
#include <new>

allocator_global_heap::allocator_global_heap()
    : mutex_(std::make_unique<std::mutex>()) {}

allocator_global_heap::~allocator_global_heap() = default;

allocator_global_heap::allocator_global_heap(allocator_global_heap const& other)
    : mutex_(std::make_unique<std::mutex>()) {
    (void)other;
}

allocator_global_heap& allocator_global_heap::operator=(allocator_global_heap const& other) {
    if (this == &other) {
        return *this;
    }
    (void)other;
    return *this;
}

allocator_global_heap::allocator_global_heap(allocator_global_heap&& other) noexcept
    : mutex_(std::make_unique<std::mutex>()) {
    (void)other;
}

allocator_global_heap& allocator_global_heap::operator=(allocator_global_heap&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    (void)other;
    return *this;
}

void* allocator_global_heap::do_allocate_sm(size_t size) {
    if (size == 0) {
        size = 1;
    }
    std::lock_guard lock{*mutex_};
    return ::operator new(size);
}

void allocator_global_heap::do_deallocate_sm(void* at) {
    if (at == nullptr) {
        return;
    }
    std::lock_guard lock{*mutex_};
    ::operator delete(at);
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}