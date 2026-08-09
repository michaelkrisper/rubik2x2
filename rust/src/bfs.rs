//! Bidirectional breadth-first search: the solver that needs no setup.
//!
//! Two frontiers grow a level at a time — one from the scramble, one from the
//! goal — and the search stops where they touch. God's number for a pocket cube
//! is 14 quarter turns and the level sizes peak at 1,350,852 states at depth 11,
//! so a one-sided search would have to walk most of the space. Meeting in the
//! middle needs depth 7 from each side, where only 33,058 states live.
//!
//! Expanding by whole levels is what makes the result optimal. When the
//! frontiers meet, every shorter pairing has already been tried, so the first
//! meeting point is a shortest solution — no heuristic, no tie-breaking.

use std::alloc::{Layout, handle_alloc_error};
#[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
use std::alloc::{alloc_zeroed, dealloc};
use std::cell::RefCell;
use std::ops::{Deref, DerefMut};
use std::ptr::NonNull;

use crate::coords::{self, N_STATES, SOLVED_INDEX};
use crate::cube::{State, invert};
use crate::error::{Error, Result};

/// How a visited state is recorded, packed into one `u16`:
///
/// | bits    | meaning                                    |
/// |---------|--------------------------------------------|
/// | `0..4`  | its distance from this frontier's root     |
/// | `4`     | which frontier claimed it                  |
/// | `5..`   | generation stamp                           |
///
/// A cube state is already a dense index below [`N_STATES`], so the visited set
/// is a flat array subscripted by the state itself rather than a hash map: no
/// hashing, no probing, and one array write per edge instead of a lookup, a
/// second lookup and an insert.
///
/// Recording a distance rather than a parent pointer is what fits the mark in
/// 16 bits: a parent needs 22. Halving the array halves the cache footprint the
/// mark loads fight over, and still leaves eleven bits of stamp, so the wrap
/// that pays for a real clear comes every 2,046 solves instead of every 63.
/// Distances are enough to rebuild the path because breadth-first marks are
/// exact: from a node at distance `d`, exactly the neighbours at `d - 1` on the
/// same side are parents, and there are only six neighbours to try.
const DEPTH_BITS: u16 = 4;
const DEPTH_MASK: u16 = (1 << DEPTH_BITS) - 1;
const SIDE_BIT: u16 = 1 << DEPTH_BITS;
const GEN_POS: u16 = DEPTH_BITS + 1;
const GEN_MASK: u16 = !(SIDE_BIT | DEPTH_MASK);
const MAX_GEN: u16 = (u16::MAX >> GEN_POS) - 1;

/// A 2 MB page holds 512 4 KB ones, and 2 MB is the x86-64 huge-page size.
const HUGE_PAGE: usize = 2 * 1024 * 1024;

/// The mark array's allocation size: 7.3 MB of `u16`s rounded up to a whole
/// number of huge pages, 8,388,608 bytes.
const MARKS_BYTES: usize = (N_STATES as usize * size_of::<u16>()).next_multiple_of(HUGE_PAGE);

/// The mark array, placed so the kernel can back it with 2 MB pages.
///
/// The marks are random accesses over ~1,800 4 KB pages — more than the dTLB
/// holds — so with ordinary pages nearly every cache miss also pays a page
/// walk. Four 2 MB pages cover the whole array and never leave the TLB. The
/// asm solver measured this at 47% of its dTLB misses, and huge pages took its
/// search from 45 to 38 µs.
///
/// On Linux this is a raw `mmap`, not an over-aligned `alloc_zeroed`, and the
/// difference is the whole optimization rather than a detail. Transparent huge
/// pages are chosen at *first fault*, so the advice has to arrive before
/// anything writes to the memory. An allocator handed an over-aligned request
/// memsets the block, which faults it in as 4 KB pages and leaves `khugepaged`
/// to collapse them later, asynchronously and often not at all: measured that
/// way this array gained 1%, against 14–18% for the mapped version. The C
/// implementation made the same mistake first and reverted it. Behaves as a
/// plain `[u16]` everywhere else, via `Deref`.
struct HugeMarks {
    ptr: NonNull<u16>,
}

#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
impl HugeMarks {
    /// Map `MARKS_BYTES` at 2 MB alignment, advise, and leave it untouched.
    ///
    /// `mmap` promises page alignment but not huge-page alignment, so this asks
    /// for one spare huge page and unmaps the misaligned head and the leftover
    /// tail — together exactly `HUGE_PAGE` bytes. Fresh anonymous mappings read
    /// as zero, which is what `Scratch` needs: generation 0 is never a live
    /// stamp, so no memset is required and none is done.
    fn new() -> Self {
        let Some(raw) = sys_mmap(MARKS_BYTES + HUGE_PAGE) else {
            handle_alloc_error(Layout::from_size_align(MARKS_BYTES, HUGE_PAGE).expect("constant"));
        };
        let base = raw.as_ptr().wrapping_add(raw.as_ptr().align_offset(HUGE_PAGE));
        let head = base as usize - raw.as_ptr() as usize;
        // SAFETY: both ranges lie inside the mapping just created and outside
        // the `MARKS_BYTES` window kept below, so nothing live is unmapped.
        unsafe {
            if head != 0 {
                sys_munmap(raw.as_ptr(), head);
            }
            sys_munmap(base.wrapping_add(MARKS_BYTES), HUGE_PAGE - head);
        }
        advise_huge(base, MARKS_BYTES);
        crate::memory::note_mapped(MARKS_BYTES);
        // SAFETY: `sys_mmap` returned a non-null address and `base` only moved
        // forward within it.
        Self { ptr: unsafe { NonNull::new_unchecked(base.cast::<u16>()) } }
    }
}

#[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
impl HugeMarks {
    /// `MARKS_BYTES` at huge-page alignment; both are non-zero constants, so
    /// the layout cannot fail.
    fn layout() -> Layout {
        Layout::from_size_align(MARKS_BYTES, HUGE_PAGE).expect("constant layout")
    }

    fn new() -> Self {
        // SAFETY: the layout has non-zero size. Zeroed is what `Scratch`
        // relies on: generation 0 is never a live stamp.
        let raw = unsafe { alloc_zeroed(Self::layout()) };
        let Some(ptr) = NonNull::new(raw.cast::<u16>()) else {
            handle_alloc_error(Self::layout());
        };
        Self { ptr }
    }
}

impl Deref for HugeMarks {
    type Target = [u16];

    fn deref(&self) -> &[u16] {
        // SAFETY: `ptr` is a live allocation of `MARKS_BYTES` zero-initialised
        // bytes, which covers `N_STATES` `u16`s, and `u16` accepts any bytes.
        unsafe { std::slice::from_raw_parts(self.ptr.as_ptr(), N_STATES as usize) }
    }
}

impl DerefMut for HugeMarks {
    fn deref_mut(&mut self) -> &mut [u16] {
        // SAFETY: as for `deref`, and `&mut self` makes the borrow exclusive.
        unsafe { std::slice::from_raw_parts_mut(self.ptr.as_ptr(), N_STATES as usize) }
    }
}

impl Drop for HugeMarks {
    fn drop(&mut self) {
        #[cfg(all(target_os = "linux", target_arch = "x86_64"))]
        // SAFETY: `ptr` is the base of a `MARKS_BYTES` mapping made by `new`,
        // and nothing else refers to it once `Scratch` is being dropped.
        unsafe {
            sys_munmap(self.ptr.as_ptr().cast::<u8>(), MARKS_BYTES);
            crate::memory::forget_mapped(MARKS_BYTES);
        }
        #[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
        // SAFETY: `ptr` came from `alloc_zeroed` with exactly this layout.
        unsafe {
            dealloc(self.ptr.as_ptr().cast::<u8>(), Self::layout());
        }
    }
}

/// `mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)`.
///
/// std has no `mmap` and the crate takes no dependencies, so this is the raw
/// syscall. Returns `None` on any error, which the caller turns into the
/// standard allocation failure.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
fn sys_mmap(len: usize) -> Option<NonNull<u8>> {
    const SYS_MMAP: usize = 9;
    const PROT_READ_WRITE: usize = 0x1 | 0x2;
    const MAP_PRIVATE_ANONYMOUS: usize = 0x02 | 0x20;
    let addr: usize;
    // SAFETY: the x86-64 Linux syscall ABI — number in rax, arguments in
    // rdi/rsi/rdx/r10/r8/r9, rcx and r11 clobbered by the instruction. Creating
    // an anonymous mapping touches nothing this program already owns.
    unsafe {
        core::arch::asm!(
            "syscall",
            inlateout("rax") SYS_MMAP => addr,
            in("rdi") 0usize,                     // let the kernel choose
            in("rsi") len,
            in("rdx") PROT_READ_WRITE,
            in("r10") MAP_PRIVATE_ANONYMOUS,
            in("r8") -1i64,                       // no file
            in("r9") 0usize,
            lateout("rcx") _,
            lateout("r11") _,
            options(nostack),
        );
    }
    // Errors come back as -errno in the top page of the address space.
    if (addr as isize) < 0 && (addr as isize) > -4096 {
        None
    } else {
        NonNull::new(addr as *mut u8)
    }
}

/// `munmap(addr, len)`, for the alignment trim and for `Drop`.
///
/// # Safety
///
/// `addr..addr + len` must be a range this process mapped and no longer uses.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
unsafe fn sys_munmap(addr: *mut u8, len: usize) {
    const SYS_MUNMAP: usize = 11;
    // SAFETY: the caller guarantees the range is ours and dead; the syscall
    // ABI is as in `sys_mmap`. A failure would leave the mapping in place,
    // which is why the result is not checked.
    unsafe {
        core::arch::asm!(
            "syscall",
            inout("rax") SYS_MUNMAP => _,
            in("rdi") addr,
            in("rsi") len,
            lateout("rcx") _,
            lateout("r11") _,
            options(nostack),
        );
    }
}

/// Ask the kernel to back `len` bytes at `addr` with huge pages.
///
/// std exposes no `madvise` and the crate takes no dependencies, so this is
/// the raw syscall. Purely a hint: the kernel may collapse the pages later or
/// not at all, and the mapping is valid either way, so the result is ignored —
/// the same best-effort stance the asm solver takes.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
fn advise_huge(addr: *mut u8, len: usize) {
    const SYS_MADVISE: usize = 28;
    const MADV_HUGEPAGE: usize = 14;
    // SAFETY: `syscall` with the x86-64 Linux ABI — number in rax, arguments
    // in rdi/rsi/rdx, rcx and r11 clobbered by the instruction itself. madvise
    // only tunes paging for memory this process already owns; it writes
    // nothing and cannot invalidate the allocation.
    unsafe {
        core::arch::asm!(
            "syscall",
            inout("rax") SYS_MADVISE => _,
            in("rdi") addr,
            in("rsi") len,
            in("rdx") MADV_HUGEPAGE,
            lateout("rcx") _,
            lateout("r11") _,
            options(nostack),
        );
    }
}

#[cfg(not(all(target_os = "linux", target_arch = "x86_64")))]
fn advise_huge(_addr: *mut u8, _len: usize) {}

/// The working set of one solve, kept alive between solves.
///
/// Allocating 7.3 MB per solve would cost far more than the search; instead a
/// generation stamp ages the marks, so a solve bumps a counter and only a wrap
/// every 2,046 solves pays for a real clear. Thread-local rather than global,
/// so concurrent callers get one each without a lock.
struct Scratch {
    marks: HugeMarks,
    generation: u16,
    buffers: [Vec<u32>; 3],
}

impl Scratch {
    fn new() -> Self {
        Self { marks: HugeMarks::new(), generation: 0, buffers: Default::default() }
    }

    /// Begin a solve; returns the tag this solve's marks carry.
    fn stamp(&mut self) -> u16 {
        if self.generation >= MAX_GEN {
            self.marks.fill(0);
            self.generation = 0;
        }
        self.generation += 1;
        self.generation << GEN_POS
    }
}

thread_local! {
    static SCRATCH: RefCell<Scratch> = RefCell::new(Scratch::new());
}

/// How many frontier entries ahead to name cache lines. Measured; the win is
/// flat from four to eight and turns back down past that.
const PREFETCH_LOOKAHEAD: usize = 6;

/// Name the mark words the loop will want shortly.
///
/// Profiling puts most of the search in one instruction: the mark load, a random
/// word of a 7.3 MB array, on machines with a few MB of last-level cache. The
/// loop therefore waits on memory rather than on arithmetic, and recomputing an
/// address early lets the misses overlap instead of stalling one at a time.
///
/// A miss to name is only a hint, so a wrong or out-of-range guess would merely
/// waste the hint -- but the bounds are checked anyway, since the cost is
/// nothing against a memory stall.
#[inline]
fn prefetch_marks(marks: &[u16], level: &[u32], position: usize, tables: &coords::Tables) {
    #[cfg(target_arch = "x86_64")]
    if let Some(&ahead) = level.get(position) {
        use std::arch::x86_64::{_MM_HINT_T0, _mm_prefetch};
        for turn in 0..tables.moves.len() {
            let child = tables.apply(ahead, turn) as usize;
            // SAFETY: a prefetch only warms a cache line; `child` is a valid
            // index into `marks`, which `apply` guarantees by construction.
            unsafe { _mm_prefetch(marks.as_ptr().add(child).cast(), _MM_HINT_T0) };
        }
    }
    #[cfg(not(target_arch = "x86_64"))]
    let _ = (marks, level, position, tables);
}

/// Return a shortest move sequence solving `state`; empty if already solved.
pub fn solve(state: &State) -> Result<String> {
    solve_within(state, crate::MAX_DEPTH)
}

/// [`solve`], with a cap on how deep the search may go.
pub fn solve_within(state: &State, max_depth: u8) -> Result<String> {
    let start = coords::encode(state)?;
    if start == SOLVED_INDEX {
        return Ok(String::new());
    }

    SCRATCH.with(|scratch| {
        let mut scratch = scratch.borrow_mut();
        let generation = scratch.stamp();
        let Scratch { marks, buffers, .. } = &mut *scratch;
        let tables = coords::tables();

        marks[start as usize] = generation;
        marks[SOLVED_INDEX as usize] = generation | SIDE_BIT;

        let [forward, backward, following] = buffers;
        forward.clear();
        forward.push(start);
        backward.clear();
        backward.push(SOLVED_INDEX);

        // The two sides advance independently — each round grows only one of
        // them — so each carries its own level depth, which the marks record.
        let mut forward_depth: u16 = 0;
        let mut backward_depth: u16 = 0;

        for _ in 0..max_depth {
            // Always grow the cheaper side, which keeps both halves shallow.
            let from_goal = backward.len() < forward.len();
            let (level, depth, side) = if from_goal {
                (&*backward, &mut backward_depth, SIDE_BIT)
            } else {
                (&*forward, &mut forward_depth, 0)
            };
            // Every state reached this round gets the same mark, so the store
            // in the inner loop is a constant.
            let reached = generation | side | (*depth + 1);

            following.clear();
            let mut meeting = None;

            'level: for (position, &node) in level.iter().enumerate() {
                prefetch_marks(marks, level, position + PREFETCH_LOOKAHEAD, tables);
                for turn in 0..tables.moves.len() {
                    let child = tables.apply(node, turn);
                    let mark = marks[child as usize];
                    if mark & GEN_MASK == generation {
                        if mark & SIDE_BIT == side {
                            continue; // this side has already been here
                        }
                        meeting = Some((child, node, turn));
                        break 'level;
                    }
                    marks[child as usize] = reached;
                    following.push(child);
                }
            }

            if let Some((met, from, turn)) = meeting {
                let here = generation | side;
                let there = here ^ SIDE_BIT;
                let mut reached = trail(marks, tables, from, here);
                reached.push(turn);
                let reached = coords::moves_to_string(reached);
                let opposite = coords::moves_to_string(trail(marks, tables, met, there));
                return Ok(if from_goal {
                    opposite + &invert(&reached)
                } else {
                    reached + &invert(&opposite)
                });
            }

            *depth += 1;
            if from_goal {
                std::mem::swap(backward, following);
            } else {
                std::mem::swap(forward, following);
            }
            if forward.is_empty() || backward.is_empty() {
                break;
            }
        }

        Err(Error::NoSolution(max_depth))
    })
}

/// Walk the marks back to this frontier's root, yielding the moves in forward
/// order: the sequence that turns that root into `node`.
///
/// `tag` is the generation and side bit the frontier's marks carry. A neighbour
/// one step shallower under the same tag is a parent by construction, and the
/// move reaching `node` from it is the inverse of the one that got there —
/// adjacent indices, which is why the solve moves pair each move with its
/// inverse.
fn trail(marks: &[u16], tables: &coords::Tables, mut node: u32, tag: u16) -> Vec<usize> {
    let mut turns = Vec::new();
    for depth in (1..=(marks[node as usize] & DEPTH_MASK)).rev() {
        let parent = tag | (depth - 1);
        for turn in 0..tables.moves.len() {
            let back = tables.apply(node, turn ^ 1);
            if marks[back as usize] == parent {
                turns.push(turn);
                node = back;
                break;
            }
        }
    }
    turns.reverse();
    turns
}
