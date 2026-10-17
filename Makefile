CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
LDLIBS ?= -pthread
SRC := $(wildcard src/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))

all: mbc

# src/gui_page.cpp is generated from web/* by embed.py (checked in; rerun after editing web/)
src/gui_page.cpp: $(wildcard web/*)
	python3 embed.py

mbc: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: mbc
	bash tests/run_tests.sh

clean:
	rm -rf build mbc mbc.exe

.PHONY: all test clean
