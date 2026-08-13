CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := bin/efgr
OFFICIAL_BIN := bin/EarlyFloorplanning_with_GlobalRoute

.PHONY: all clean static

all: $(BIN)

# Contest submission build: fully static so the binary runs on the official
# eval machine (glibc 2.28-era; dynamic builds from this toolchain loader-fail).
static:
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -static -static-libgcc -static-libstdc++ -o $(OFFICIAL_BIN) $(SRC)

$(BIN): $(OBJ)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)
	cp $@ $(OFFICIAL_BIN)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(BIN) $(OFFICIAL_BIN)
