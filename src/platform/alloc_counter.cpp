// Heap-allocation counting, for the "no allocations in WM_PAINT" rule. Copied from foo_mediabar.
//
// The global operator new / delete are replaced for this module only: this DLL's translation
// units and the SDK/pfc static libraries linked into it. Allocations inside other DLLs (Direct2D,
// DirectWrite, the host) are not counted. Every overload the language can pick is here, or the
// compiler silently keeps the CRT's version for the missing ones and the count is wrong.
// Cost: one relaxed atomic increment per allocation.

#include "perf.h"

#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace {

std::atomic<std::uint64_t> g_allocations{0};

[[nodiscard]] void* counted(void* pointer) noexcept {
    if (pointer != nullptr) g_allocations.fetch_add(1, std::memory_order_relaxed);
    return pointer;
}

[[nodiscard]] std::size_t at_least_one(std::size_t size) noexcept { return size == 0 ? 1 : size; }

} // namespace

namespace bettertabs::perf {

std::uint64_t allocation_count() noexcept { return g_allocations.load(std::memory_order_relaxed); }

} // namespace bettertabs::perf

void* operator new(std::size_t size) {
    void* pointer = std::malloc(at_least_one(size));
    if (pointer == nullptr) throw std::bad_alloc{};
    return counted(pointer);
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return counted(std::malloc(at_least_one(size)));
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

void* operator new(std::size_t size, std::align_val_t alignment) {
    void* pointer = _aligned_malloc(at_least_one(size), static_cast<std::size_t>(alignment));
    if (pointer == nullptr) throw std::bad_alloc{};
    return counted(pointer);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return counted(_aligned_malloc(at_least_one(size), static_cast<std::size_t>(alignment)));
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& tag) noexcept {
    return ::operator new(size, alignment, tag);
}

void operator delete(void* pointer, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { _aligned_free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    _aligned_free(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    _aligned_free(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    _aligned_free(pointer);
}
