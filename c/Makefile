# rubik2x2 -- optimal 2x2x2 solver in C23.
#
#   make            build ./rubik2x2
#   make test       build and run the test suite
#   make bench      run the benchmark against bench/scrambles.txt
#   make clean

CC ?= gcc

STANDARD := -std=c23
WARNINGS := -Wall -Wextra -Werror
OPTIMISE := -O3 -march=native -flto
BUILD_ID := C23, -O3 -march=native -flto

# -std=c23 is strict ISO, which hides POSIX declarations (pthread barriers,
# clock_gettime, isatty). Asking for POSIX.1-2008 explicitly brings them back
# without falling back to a -std=gnu dialect.
CPPFLAGS += -Isrc -D_POSIX_C_SOURCE=200809L -DRUBIK_BUILD_FLAGS='"$(BUILD_ID)"'
CFLAGS   += $(STANDARD) $(WARNINGS) $(OPTIMISE) -pthread
LDFLAGS  += $(OPTIMISE) -pthread
LDLIBS   +=

BUILD := build
PROGRAM := rubik2x2
TEST := $(BUILD)/test_rubik
SCRAMBLES := ../bench/scrambles.txt

SOURCES := $(wildcard src/*.c)
LIBRARY_SOURCES := $(filter-out src/cli.c,$(SOURCES))
LIBRARY_OBJECTS := $(LIBRARY_SOURCES:%.c=$(BUILD)/%.o)
CLI_OBJECT := $(BUILD)/src/cli.o
TEST_OBJECT := $(BUILD)/tests/test_rubik.o
OBJECTS := $(LIBRARY_OBJECTS) $(CLI_OBJECT) $(TEST_OBJECT)

.PHONY: all test bench clean
.DEFAULT_GOAL := all

all: $(PROGRAM)

$(PROGRAM): $(LIBRARY_OBJECTS) $(CLI_OBJECT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST): $(LIBRARY_OBJECTS) $(TEST_OBJECT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

test: $(TEST)
	./$(TEST)

bench: $(PROGRAM)
	./$(PROGRAM) bench --scrambles $(SCRAMBLES)

clean:
	$(RM) -r $(BUILD) $(PROGRAM)

-include $(OBJECTS:.o=.d)
