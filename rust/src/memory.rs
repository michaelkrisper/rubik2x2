//! Peak-allocation accounting, for the benchmark's `peak_mb` column.
//!
//! Rust has no equivalent of `tracemalloc`, but it does let a program replace
//! the global allocator — so the honest way to answer "how much memory did that
//! solve hold?" is to count. [`Tracking`] delegates every request to the system
//! allocator and keeps a running total plus a high-water mark; the binary
//! installs it as `#[global_allocator]`. If nothing installs it (unit tests,
//! library users) the counters stay at zero and the benchmark reports 0.0.
//!
//! The cost is two relaxed atomic operations per allocation, which does not
//! show up against a solve that allocates a handful of times.

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering::Relaxed};

static LIVE: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

/// The system allocator, counted.
pub struct Tracking;

impl Tracking {
    fn grew(size: usize) {
        let live = LIVE.fetch_add(size, Relaxed) + size;
        PEAK.fetch_max(live, Relaxed);
    }

    fn shrank(size: usize) {
        LIVE.fetch_sub(size, Relaxed);
    }
}

// SAFETY: every method forwards to `System`, which upholds the contract, and
// only adds bookkeeping that cannot affect the returned pointers. `unsafe` is
// unavoidable here — `GlobalAlloc` is an unsafe trait by definition — and it
// buys the only real measurement of peak memory available to a std-only build.
unsafe impl GlobalAlloc for Tracking {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let pointer = unsafe { System.alloc(layout) };
        if !pointer.is_null() {
            Self::grew(layout.size());
        }
        pointer
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let pointer = unsafe { System.alloc_zeroed(layout) };
        if !pointer.is_null() {
            Self::grew(layout.size());
        }
        pointer
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        Self::shrank(layout.size());
        unsafe { System.dealloc(pointer, layout) }
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        let moved = unsafe { System.realloc(pointer, layout, new_size) };
        if !moved.is_null() {
            if new_size >= layout.size() {
                Self::grew(new_size - layout.size());
            } else {
                Self::shrank(layout.size() - new_size);
            }
        }
        moved
    }
}

/// Count memory this program obtained without going through the allocator.
///
/// The BFS mark array is mapped with `mmap` rather than allocated, because the
/// huge-page advice has to reach the pages before anything faults them in — so
/// [`Tracking`] never sees it, and `peak_mb` would report a saving that is not
/// real. The mapping reports itself here instead.
pub fn note_mapped(bytes: usize) {
    Tracking::grew(bytes);
}

/// The counterpart to [`note_mapped`], for when the mapping is released.
pub fn forget_mapped(bytes: usize) {
    Tracking::shrank(bytes);
}

/// Bytes currently allocated.
pub fn live() -> usize {
    LIVE.load(Relaxed)
}

/// The high-water mark since the last [`reset_peak`].
pub fn peak() -> usize {
    PEAK.load(Relaxed)
}

/// Forget the high-water mark and start again from what is live right now.
pub fn reset_peak() {
    PEAK.store(LIVE.load(Relaxed), Relaxed);
}
