#include "core/Memory.h"

#if defined(VOTERPOOL_WITH_JEMALLOC)
#include <jemalloc/jemalloc.h>
#include <new>
#endif

namespace voterpool::Memory {

bool configure() {
#if defined(VOTERPOOL_WITH_JEMALLOC)
    size_t sz = sizeof(size_t);
    return mallctl("version", nullptr, nullptr, nullptr, 0) == 0;
#else
    return false;
#endif
}

}  // namespace voterpool::Memory

#if defined(VOTERPOOL_WITH_JEMALLOC)
void* operator new(std::size_t size) {
    void* p = malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return malloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return malloc(size); }
void* operator new(std::size_t size, std::align_val_t al) {
    void* p = aligned_alloc(static_cast<std::size_t>(al), size);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t size, std::align_val_t al) { return ::operator new(size, al); }
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept { return aligned_alloc(static_cast<std::size_t>(al), size); }
void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept { return aligned_alloc(static_cast<std::size_t>(al), size); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, std::size_t) noexcept { free(p); }
void operator delete[](void* p, std::size_t) noexcept { free(p); }
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, std::align_val_t, std::size_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t, std::size_t) noexcept { free(p); }
#endif
