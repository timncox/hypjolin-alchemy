# Native tests for the Hypjolin engine (host compiler, no hardware).
#   make            ASan+UBSan build, run
#   make fast       -O2 without sanitizers
CXX      ?= c++
SRC      := ../src/hypjolin/hypjolin_engine.cpp
HDR      := ../src/hypjolin/hypjolin_engine.h
CXXFLAGS := -std=gnu++17 -O2 -g -I../src/hypjolin -Wall -Wextra
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
BIN      := bin

.PHONY: all fast
all: $(BIN)/test_hypjolin
	./$(BIN)/test_hypjolin

fast: $(BIN)/test_hypjolin_fast
	./$(BIN)/test_hypjolin_fast

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_hypjolin: test_hypjolin.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) $(SAN) test_hypjolin.cpp $(SRC) -o $@

$(BIN)/test_hypjolin_fast: test_hypjolin.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) test_hypjolin.cpp $(SRC) -o $@
