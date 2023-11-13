# Compiler
CXX := g++
# Compiler flags
CXXFLAGS := -std=c++17 -Wall -Wextra

# Source and header files
SRC := MemoryManager.cpp
HEADER := MemoryManager.h

# Output static library
LIBRARY := libMemoryManager.a

all: clean $(LIBRARY)

$(LIBRARY): MemoryManager.cpp
	$(CXX) $(CXXFLAGS) -c $(SRC) -o $(LIBRARY)

clean:
	rm -f $(LIBRARY)

.PHONY: all clean

