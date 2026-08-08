# rubik2x2 -- optimal 2x2x2 solver in hand-written x86-64 assembly.
#
#   make            build ./rubik2x2
#   make test       build and run the test suite
#   make bench      run the benchmark against bench/scrambles.txt
#   make clean
#
# There is no compiler in this build: `as` assembles, and `gcc` is used only as
# the linker driver so that the C runtime start-up files and libc come along.
# No C source of this project is compiled or linked.

AS      := as
LD      := gcc
ASFLAGS := --64 -I src -g
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

test: $(TEST)
	./$(TEST)

bench: $(PROGRAM)
	./$(PROGRAM) bench --scrambles $(SCRAMBLES)

clean:
	$(RM) -r $(BUILD) $(PROGRAM)
