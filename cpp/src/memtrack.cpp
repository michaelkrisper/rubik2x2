#include "rubik/memtrack.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

/// Every block carries the size the caller asked for and the pointer malloc
/// actually returned, so an unsized `operator delete` can still account for it
/// and over-aligned requests need no separate path.
struct Header {
    void* base;
    std::size_t size;
};

// Constant-initialised, so the counters are valid before any static
// constructor -- including the ones that allocate.
std::atomic<std::size_t> live{0};
std::atomic<std::size_t> peak{0};
std::atomic<std::size_t> baseline{0};

void note_allocation(std::size_t bytes) {
    const std::size_t total = live.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::size_t seen = peak.load(std::memory_order_relaxed);
    while (total > seen && !peak.compare_exchange_weak(seen, total, std::memory_order_relaxed)) {
    }
}

void* allocate(std::size_t size, std::size_t alignment) noexcept {
    if (alignment < alignof(Header)) {
        alignment = alignof(Header);
    }
    void* base = std::malloc(size + alignment + sizeof(Header));
    if (base == nullptr) {
        return nullptr;
    }
    auto address = reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
    address = (address + alignment - 1) & ~static_cast<std::uintptr_t>(alignment - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    header->base = base;
    header->size = size;
    note_allocation(size);
    return reinterpret_cast<void*>(address);
}

void release(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    Header* header = static_cast<Header*>(pointer) - 1;
    live.fetch_sub(header->size, std::memory_order_relaxed);
    std::free(header->base);
}

void* allocate_or_throw(std::size_t size, std::size_t alignment) {
    void* pointer = allocate(size, alignment);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    return pointer;
}

}  // namespace

namespace rubik::memtrack {

void reset() {
    const std::size_t current = live.load(std::memory_order_relaxed);
    baseline.store(current, std::memory_order_relaxed);
    peak.store(current, std::memory_order_relaxed);
}

std::size_t peak_bytes() {
    const std::size_t high = peak.load(std::memory_order_relaxed);
    const std::size_t base = baseline.load(std::memory_order_relaxed);
    return high > base ? high - base : 0;
}

std::size_t live_bytes() {
    const std::size_t current = live.load(std::memory_order_relaxed);
    const std::size_t base = baseline.load(std::memory_order_relaxed);
    return current > base ? current - base : 0;
}

void note_mapped(std::size_t bytes) { note_allocation(bytes); }

void forget_mapped(std::size_t bytes) { live.fetch_sub(bytes, std::memory_order_relaxed); }

}  // namespace rubik::memtrack

void* operator new(std::size_t size) { return allocate_or_throw(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new[](std::size_t size) { return allocate_or_throw(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_or_throw(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_or_throw(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    release(pointer);
}
