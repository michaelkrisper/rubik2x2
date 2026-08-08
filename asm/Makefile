# rubik2x2 -- optimal 2x2x2 solver in hand-written x86-64 assembly.
#
#   make            build ./rubik2x2
#   make test       build and run the test suite
#   make bench      run the benchmark against bench/scrambles.txt
#   make clean
#
# Nothing that ships is compiled: `as` assembles every line of the program, and
# `gcc` is used only as the linker driver so that the C runtime start-up files
# and libc come along.  No C source is compiled into ./rubik2x2.
#
# The test runner is the one exception, and it is not part of the program: it
# is C that calls the assembly routines from outside, so a failing assertion
# can say what it expected.  The spec asks for the algorithms to be assembly,
# not the harness.

AS      := as
CC      := gcc
LD      := gcc
ASFLAGS := --64 -I src -g
CFLAGS  := -std=c17 -O2 -g -Wall -Wextra
LDFLAGS := -pthread
LDLIBS  :=

BUILD     := build
PROGRAM   := rubik2x2
TEST      := $(BUILD)/test_rubik
SCRAMBLES := ../bench/scrambles.txt

LIB_SOURCES  := src/cube.s src/coords.s src/bfs.s src/ida.s src/table.s src/bench.s
LIB_OBJECTS  := $(LIB_SOURCES:%.s=$(BUILD)/%.o) $(BUILD)/version.o
CLI_OBJECT   := $(BUILD)/src/cli.o
TEST_OBJECT  := $(BUILD)/tests/test_rubik.o

AS_VERSION := $(shell $(AS) --version | head -1 | sed 's/.*version //')
BUILD_ID   := GNU as $(AS_VERSION), hand-written x86-64, System V ABI

.PHONY: all test bench clean
.DEFAULT_GOAL := all

all: $(PROGRAM)

$(PROGRAM): $(LIB_OBJECTS) $(CLI_OBJECT)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST): $(LIB_OBJECTS) $(TEST_OBJECT)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# The assembler has no preprocessor, so the version strings the benchmark
# reports are generated here rather than baked into a source file.
$(BUILD)/version.s:
	@mkdir -p $(dir $@)
	@printf '\t.section .rodata\n' > $@
	@printf '\t.globl bench_version\nbench_version:\t.asciz "%s"\n' '$(AS_VERSION)' >> $@
	@printf '\t.globl bench_build\nbench_build:\t.asciz "%s"\n' '$(BUILD_ID)' >> $@
	@printf '\t.section .note.GNU-stack,"",@progbits\n' >> $@

$(BUILD)/version.o: $(BUILD)/version.s
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/%.o: %.s src/rubik.inc
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

# The test runner, and only the test runner, goes through a C compiler.
$(BUILD)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST)
	./$(TEST)

bench: $(PROGRAM)
	./$(PROGRAM) bench --scrambles $(SCRAMBLES)

clean:
	$(RM) -r $(BUILD) $(PROGRAM)
