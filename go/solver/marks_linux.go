//go:build linux

package solver

import (
	"syscall"
	"unsafe"
)

// hugePage is the 2 MB unit transparent huge pages come in on x86-64.
const hugePage = 2 << 20

// newMarks allocates the mark table backed by transparent huge pages.
//
// The table is 7.3 MB of random access — one word per edge at an effectively
// arbitrary index — spread over ~1,800 4 KB pages, more than the dTLB holds,
// so every cache miss also paid a page walk. On 2 MB pages the whole table
// needs four TLB entries. mmap promises only 4 KB alignment, so the mapping
// carries one spare huge page and the working window is the first 2 MB-aligned
// run inside it; the standard library cannot partially munmap, so the slack
// simply stays mapped — up to 2 MB of virtual address space, never touched,
// never paged in. Nothing ever unmaps the region, which mirrors the previous
// make(): a pooled scratch keeps its marks for the life of the process.
func newMarks(n int) []uint16 {
	bytes := (2*n + hugePage - 1) &^ (hugePage - 1)
	b, err := syscall.Mmap(-1, 0, bytes+hugePage,
		syscall.PROT_READ|syscall.PROT_WRITE,
		syscall.MAP_PRIVATE|syscall.MAP_ANON)
	if err != nil {
		return make([]uint16, n)
	}
	mappedBytes.Add(int64(len(b)))
	addr := uintptr(unsafe.Pointer(&b[0]))
	window := b[-addr&(hugePage-1):][:bytes]
	// Best effort: with THP in madvise mode the huge pages arrive as the
	// zeroed mapping is first touched; if the kernel declines, the window
	// still works as ordinary 4 KB pages.
	_ = syscall.Madvise(window, syscall.MADV_HUGEPAGE)
	// The window is fresh, zeroed, uint16-aligned memory nothing else
	// aliases, so reinterpreting its bytes is safe; mmap'd memory is not
	// garbage collected, so the slice needs no keep-alive.
	return unsafe.Slice((*uint16)(unsafe.Pointer(&window[0])), n)
}
