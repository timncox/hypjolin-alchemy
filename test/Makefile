# Native tests for the Hypjolin engine and the Benjolin build (host
# compiler, no hardware).
#   make            ASan+UBSan builds, run both
#   make fast       -O2 without sanitizers; also asserts the Benjolin's
#                   per-block cost does not follow RATE (timing is only
#                   meaningful unsanitised)
CXX      ?= c++
SRC      := ../src/hypjolin/hypjolin_engine.cpp
HDR      := ../src/hypjolin/hypjolin_engine.h
BHDR     := ../src/benjolin/benjolin_map.h
CXXFLAGS := -std=gnu++17 -O2 -g -I../src/hypjolin -I../src/benjolin -Wall -Wextra
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
BIN      := bin

.PHONY: all fast
all: $(BIN)/test_hypjolin $(BIN)/test_benjolin
	./$(BIN)/test_hypjolin
	./$(BIN)/test_benjolin

fast: $(BIN)/test_hypjolin_fast $(BIN)/test_benjolin_fast
	./$(BIN)/test_hypjolin_fast
	./$(BIN)/test_benjolin_fast

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_hypjolin: test_hypjolin.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) $(SAN) test_hypjolin.cpp $(SRC) -o $@

$(BIN)/test_hypjolin_fast: test_hypjolin.cpp $(SRC) $(HDR) | $(BIN)
	$(CXX) $(CXXFLAGS) test_hypjolin.cpp $(SRC) -o $@

$(BIN)/test_benjolin: test_benjolin.cpp $(SRC) $(HDR) $(BHDR) | $(BIN)
	$(CXX) $(CXXFLAGS) $(SAN) test_benjolin.cpp $(SRC) -o $@

$(BIN)/test_benjolin_fast: test_benjolin.cpp $(SRC) $(HDR) $(BHDR) | $(BIN)
	$(CXX) $(CXXFLAGS) -DTIMING_CHECKS test_benjolin.cpp $(SRC) -o $@
