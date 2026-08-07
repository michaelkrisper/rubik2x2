# rubik2x2 -- optimal 2x2x2 solver, C++23.
#
# CMake is not installed on the reference machine, so the build is a plain
# Makefile: one library of objects, one executable, one test binary.
#
#   make            build ./rubik2x2
#   make test       build and run the test binary
#   make clean

CXX ?= g++

STANDARD := -std=c++23
OPTIMISE := -O3 -march=native -flto
WARNINGS := -Wall -Wextra -Werror

BUILD_DESCRIPTION := $(CXX) $(STANDARD) $(OPTIMISE)

CPPFLAGS += -Iinclude -MMD -MP -DRUBIK_BUILD_FLAGS='"$(BUILD_DESCRIPTION)"'
CXXFLAGS += $(STANDARD) $(OPTIMISE) $(WARNINGS) -pthread
LDFLAGS  += $(STANDARD) $(OPTIMISE) -pthread

BUILD := build
LIBRARY_SOURCES := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
TEST_SOURCES := $(wildcard tests/*.cpp)

LIBRARY_OBJECTS := $(LIBRARY_SOURCES:%.cpp=$(BUILD)/%.o)
MAIN_OBJECT := $(BUILD)/src/main.o
TEST_OBJECTS := $(TEST_SOURCES:%.cpp=$(BUILD)/%.o)

.PHONY: all test clean
.DEFAULT_GOAL := all

all: rubik2x2

rubik2x2: $(LIBRARY_OBJECTS) $(MAIN_OBJECT)
	$(CXX) $(LDFLAGS) $^ -o $@

rubik2x2_tests: $(LIBRARY_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

test: rubik2x2_tests
	./rubik2x2_tests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Itests -c $< -o $@

clean:
	rm -rf $(BUILD) rubik2x2 rubik2x2_tests

-include $(LIBRARY_OBJECTS:.o=.d) $(MAIN_OBJECT:.o=.d) $(TEST_OBJECTS:.o=.d)
