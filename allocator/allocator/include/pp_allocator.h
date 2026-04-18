//
// Created by Des Caldnd on 6/29/2024.
//

#ifndef SYS_PROG_PP_ALLOCATOR_H
#define SYS_PROG_PP_ALLOCATOR_H

#include <cstddef>
#include <memory>
#include <new>

#if __has_include(<memory_resource>)
#include <memory_resource>
#else
namespace std::pmr
{
    class memory_resource
    {
    public:
        virtual ~memory_resource() = default;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
        {
            return do_allocate(bytes, alignment);
        }

        void deallocate(void* p, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
        {
            do_deallocate(p, bytes, alignment);
        }

        [[nodiscard]] bool is_equal(const memory_resource& other) const noexcept
        {
            return do_is_equal(other);
        }

    private:
        virtual void* do_allocate(std::size_t bytes, std::size_t alignment) = 0;
        virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) = 0;
        virtual bool do_is_equal(const memory_resource& other) const noexcept = 0;
    };

    inline bool operator==(const memory_resource& lhs, const memory_resource& rhs) noexcept
    {
        return &lhs == &rhs || lhs.is_equal(rhs);
    }

    inline bool operator!=(const memory_resource& lhs, const memory_resource& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    class new_delete_resource_impl final : public memory_resource
    {
    private:
        void* do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                return ::operator new(bytes, std::align_val_t(alignment));
            }

            return ::operator new(bytes);
        }

        void do_deallocate(void* p, std::size_t, std::size_t alignment) override
        {
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                ::operator delete(p, std::align_val_t(alignment));
                return;
            }

            ::operator delete(p);
        }

        bool do_is_equal(const memory_resource& other) const noexcept override
        {
            return this == &other;
        }
    };

    inline memory_resource* new_delete_resource() noexcept
    {
        static new_delete_resource_impl resource;
        return &resource;
    }

    inline memory_resource*& default_resource_instance() noexcept
    {
        static memory_resource* current = new_delete_resource();
        return current;
    }

    inline memory_resource* null_memory_resource() noexcept
    {
        return nullptr;
    }

    inline memory_resource* set_default_resource(memory_resource* resource) noexcept
    {
        memory_resource* previous = default_resource_instance();
        if (resource != nullptr)
        {
            default_resource_instance() = resource;
        }

        return previous;
    }

    inline memory_resource* get_default_resource() noexcept
    {
        return default_resource_instance();
    }
}
#endif

struct smart_mem_resource : public std::pmr::memory_resource
{
private:
    virtual void do_deallocate_sm(void*) =0;

    void do_deallocate(void* p, size_t, size_t) final;

    virtual void* do_allocate_sm(size_t) =0;

    void * do_allocate(size_t _Bytes, size_t _Align) final;
};



struct test_mem_resource : public smart_mem_resource
{
private:

    void* do_allocate_sm(size_t n) override;
    void do_deallocate_sm(void* p) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
};

// propagating_polymorphic_allocator
template <typename T>
struct pp_allocator
{
private:
    std::pmr::memory_resource* _mem;

public:

    using propagate_on_container_swap = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using value_type = T;

    pp_allocator(const pp_allocator& other) noexcept =default;
    pp_allocator(std::pmr::memory_resource* mem = std::pmr::get_default_resource()) noexcept;

    template<class U>
    pp_allocator(const pp_allocator<U>& other) noexcept;

    pp_allocator& operator=(const pp_allocator&) =default;
    pp_allocator& operator=(pp_allocator&&) noexcept =default;
    ~pp_allocator() =default;

    [[nodiscard]] T* allocate(size_t n);
    void deallocate(T* p, size_t n = 1);

    template<class U, class... Args>
    void construct(U* p, Args&&... args);

    template<class U>
    void destroy(U* p);

    [[nodiscard]] void* allocate_bytes(size_t nbytes, size_t alignment = alignof(std::max_align_t));

    void deallocate_bytes(void* p, size_t bytes = 1, size_t alignment = alignof(std::max_align_t));

    template< class U >
    [[nodiscard]] U* allocate_object( std::size_t n = 1 );

    template< class U >
    void deallocate_object( U* p, std::size_t n = 1 );

    template< class U, class... CtorArgs >
    [[nodiscard]] U* new_object( CtorArgs&&... ctor_args );

    template< class U >
    void delete_object( U* p );

    pp_allocator select_on_container_copy_construction() const;

    std::pmr::memory_resource* resource() const;


};

template<typename T>
bool operator==(const pp_allocator<T>& lhs, const pp_allocator<T>& rhs) noexcept
{
    return *lhs.resource() == *rhs.resource();
}

template<typename T>
bool operator!=(const pp_allocator<T>& lhs, const pp_allocator<T>& rhs) noexcept
{
    return !(lhs == rhs);
}

template<typename T>
pp_allocator<T>::pp_allocator(std::pmr::memory_resource *mem) noexcept : _mem(mem == nullptr ? std::pmr::get_default_resource() : mem) {}

template<typename T>
std::pmr::memory_resource *pp_allocator<T>::resource() const
{
    return _mem;
}

template<typename T>
pp_allocator<T> pp_allocator<T>::select_on_container_copy_construction() const
{
    return pp_allocator(resource());
}

template<typename T>
template<class U>
void pp_allocator<T>::delete_object(U *p)
{
    destroy(p);
    deallocate_object(p);
}

template<typename T>
template<class U, class... CtorArgs>
U *pp_allocator<T>::new_object(CtorArgs &&... ctor_args)
{
    U* p = allocate_object<U>();
    try
    {
        construct(p, std::forward<CtorArgs>(ctor_args)...);
    }
    catch (...)
    {
        deallocate_object(p);
        throw;
    }
    return p;
}

template<typename T>
template<class U>
void pp_allocator<T>::deallocate_object(U *p, std::size_t n)
{
    deallocate_bytes(p, n * sizeof(U), alignof(U));
}

template<typename T>
template<class U>
U *pp_allocator<T>::allocate_object(std::size_t n)
{
    if ((std::numeric_limits<size_t>::max() / sizeof(U)) < n)
        throw std::bad_array_new_length();
    return reinterpret_cast<U*>(allocate_bytes(n * sizeof(U), alignof(U)));
}

template<typename T>
void pp_allocator<T>::deallocate_bytes(void *p, size_t bytes, size_t alignment)
{
    resource()->deallocate(p, bytes, alignment);
}

template<typename T>
void *pp_allocator<T>::allocate_bytes(size_t nbytes, size_t alignment)
{
    return resource()->allocate(nbytes, alignment);
}

template<typename T>
template<class U>
void pp_allocator<T>::destroy(U *p)
{
    p->~U();
}

template<typename T>
template<class U, class... Args>
void pp_allocator<T>::construct(U *p, Args &&... args)
{
    ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
}

template<typename T>
void pp_allocator<T>::deallocate(T *p, size_t n)
{
    _mem->deallocate(p, n * sizeof(T), alignof(T));
}

template<typename T>
T *pp_allocator<T>::allocate(size_t n)
{
    return reinterpret_cast<T*>(_mem->allocate(n * sizeof(T), alignof(T)));
}

template <typename T>
template <typename U>
pp_allocator<T>::pp_allocator(const pp_allocator<U>& other) noexcept : _mem(other.resource())
{}



#endif //SYS_PROG_PP_ALLOCATOR_H
