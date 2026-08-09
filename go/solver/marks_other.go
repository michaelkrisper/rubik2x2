//go:build !linux

package solver

// newMarks allocates the mark table; only Linux backs it with huge pages.
func newMarks(n int) []uint16 { return make([]uint16, n) }
