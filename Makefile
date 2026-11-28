# Local developer shortcuts. CI runs the same presets (see .github/workflows/ci.yml).

# Keep these in sync with .github/workflows/ci.yml.
JSON_REVISION ?= 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
GOOGLETEST_REVISION ?= 8eff9e336692fc95961e096564f1044c600b881d

PRESET ?= debug
NODE ?= node
# Directory opened by `make example-ide`.
DIR ?= .

.PHONY: all deps configure build test test-native test-browser test-sanitize example-ide clean

all: build

libs/json:
	git clone --filter=blob:none https://github.com/nlohmann/json.git $@
	git -C $@ checkout $(JSON_REVISION)

libs/googletest:
	git clone --filter=blob:none https://github.com/google/googletest.git $@
	git -C $@ checkout $(GOOGLETEST_REVISION)

deps: libs/json libs/googletest

configure: deps
	cmake --preset $(PRESET)

build: configure
	cmake --build --preset $(PRESET) --parallel

test-native: build
	ctest --preset $(PRESET) --output-on-failure

test-browser:
	@$(NODE) -e 'if (+process.versions.node.split(".")[0] < 20) { console.error(`Node >= 20 required (found $${process.version}); set NODE=...`); process.exit(1); }'
	$(NODE) --test tests/browser/*.mjs

test: test-native test-browser

test-sanitize:
	ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	$(MAKE) test-native PRESET=sanitize

# Builds and runs examples/ide on DIR. Over SSH it only prints the launch URL; forward the port and open it locally.
example-ide: deps
	cmake -S examples/ide -B build-example-ide
	cmake --build build-example-ide --parallel
	build-example-ide/liteapp-ide "$(DIR)" 

clean:
	rm -rf build-debug build-sanitize build-example-ide
