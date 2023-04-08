WITH =
ARGS =

help:
	@echo "help"

.PHONY: help do-cmake build clean run build-test test
do-cmake:
	[ -d build ] || mkdir build
	cd build && cmake ..

build: do-cmake
	cd build && make -j

build-run: build run

build-test:
	mkdir -p build
	cd build && cmake .. -Dlumidb_test=on
	cd build && make

	cd build && make test

run:
	./build/lumidb ${ARGS}

test:
	cd build && make test

CASE = test_unit
test-case:
	@cd build && make -j4 1>/dev/null
	@./build/test/${CASE}

clean:
	rm -r build
