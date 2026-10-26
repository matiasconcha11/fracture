BUILD ?= build
CONFIG ?= Release

.PHONY: build debug test validate check clean

build:
	cmake -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=$(CONFIG)
	cmake --build $(BUILD)

debug:
	$(MAKE) build BUILD=build-debug CONFIG=Debug

test: build
	ctest --test-dir $(BUILD) --output-on-failure

validate: build
	scripts/pluginval.sh $(BUILD)

check: test validate

clean:
	rm -rf build build-debug
