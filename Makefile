PLUGIN_NAME = hyprdecor

all: release

release:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build

debug:
	cmake -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build

clean:
	rm -rf build

install: release
	cmake --install build

.PHONY: all release debug clean install
