.PHONY: all run clean

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build --parallel

run: all
	./build/network_simulator

clean:
	rm -rf build
