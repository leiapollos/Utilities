#pragma once

#include "utils_helpers.h"

#ifdef _WIN32
extern "C" {
    __declspec(dllimport) void* __stdcall VirtualAlloc(void* addr, unsigned long size,
        unsigned long type, unsigned long protect);
    __declspec(dllimport) int __stdcall VirtualFree(void* addr, unsigned long size,
        unsigned long freeType);
}
static constexpr unsigned long MEM_COMMIT = 0x1000;
static constexpr unsigned long MEM_RESERVE = 0x2000;
static constexpr unsigned long MEM_RELEASE = 0x8000;
static constexpr unsigned long PAGE_READWRITE = 0x04;
#else
extern "C" {
    void* mmap(void* addr, unsigned long length, int prot, int flags, int fd, long offset);
    int munmap(void* addr, unsigned long length);
}
static constexpr int PROT_READ = 0x1;
static constexpr int PROT_WRITE = 0x2;
static constexpr int MAP_PRIVATE = 0x02;
static constexpr int MAP_ANONYMOUS = 0x20;
#endif

namespace utils {
    struct PreTouchEnabledPolicy {
        static constexpr bool preTouchEnabled = true;
    };

    struct PreTouchDisabledPolicy {
        static constexpr bool preTouchEnabled = false;
    };

    static constexpr size_t CACHE_LINE_SIZE = 64;

    inline void* os_alloc(size_t size, bool doPreTouch) {
        static const size_t pageSize = utils::get_page_size();
        size_t alignedSize = (size + (pageSize - 1)) & ~(pageSize - 1);

#ifdef _WIN32
        void* ptr = VirtualAlloc(nullptr, static_cast<unsigned long>(alignedSize),
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!ptr) return nullptr;
#else
        void* ptr = mmap(nullptr, alignedSize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == (void*)-1) return nullptr;
#endif

        if (doPreTouch) {
            utils::page_touch(ptr, alignedSize);
        }
        return ptr;
    }

    inline void os_free(void* ptr, size_t size) {
        if (!ptr || size == 0) return;

#ifdef _WIN32
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        static const size_t pageSize = utils::get_page_size();
        size_t alignedSize = (size + (pageSize - 1)) & ~(pageSize - 1);
        munmap(ptr, alignedSize);
#endif
    }

    template<typename T, class PreTouchPolicy = PreTouchDisabledPolicy>
    class standard_allocator {
    public:
        typedef T value_type;

        inline T* allocate(size_t n) {
            size_t bytes = n * sizeof(T);
            void* mem = ::operator new(bytes);
            if (!mem) return nullptr;

            if constexpr (PreTouchPolicy::preTouchEnabled) {
                utils::page_touch(mem, bytes);
            }
            return static_cast<T*>(mem);
        }

        inline void deallocate(T* ptr, size_t /*n*/) {
            if (!ptr) return;
            ::operator delete(ptr);
        }

        inline void construct(T* ptr, const T& val) {
            new (static_cast<void*>(ptr)) T(val);
        }

        inline void destroy(T* ptr) {
            if (!ptr) return;
            ptr->~T();
        }
    };

    template<class PreTouchPolicy = PreTouchDisabledPolicy>
    class linear_allocator {
    public:
        inline linear_allocator(size_t size)
            : _bufferSize(size), _offset(0) {
            if constexpr (PreTouchPolicy::preTouchEnabled) {
                _buffer = static_cast<uint8_t*>(os_alloc(_bufferSize, true));
            }
            else {
                _buffer = static_cast<uint8_t*>(os_alloc(_bufferSize, false));
            }
        }

        inline ~linear_allocator() {
            os_free(_buffer, _bufferSize);
        }

        template<typename T, typename... Args>
        T* allocate(Args&&... args) {
            const size_t alignment = alignof(T);
            const size_t bytes = sizeof(T);

            void* current = _buffer + _offset;
            void* aligned = utils::align_forward(current, alignment);

            uintptr_t newOffset = reinterpret_cast<uintptr_t>(aligned) + bytes - reinterpret_cast<uintptr_t>(_buffer);
            if (newOffset > _bufferSize) {
                return nullptr;
            }
            _offset = newOffset;

            return new (aligned) T(utils::forward<Args>(args)...);
        }

        inline void deallocate(void* /*ptr*/, size_t /*bytes*/) {
            // No-op in a linear allocator
        }

        inline void reset() {
            _offset = 0;
        }

    private:
        uint8_t* _buffer;
        size_t _bufferSize;
        size_t _offset;
    };

}