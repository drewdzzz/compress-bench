SRC1 = benchmark.cpp
SRC2 = use_data.cpp
OUT = benchmark

all: build run

build: $(SRC1) $(SRC2)
	$(CXX) -std=c++17 -llz4 $(SRC1) $(SRC2) -o $(OUT)

run:
	./$(OUT)
