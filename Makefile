WITH =
ARGS =

.PHONY: help test e2e

help:
	@echo "help"

.PHONY: help do-cmake build clean run build-test test
do-cmake:
	[ -d build ] || mkdir build
	cd build && cmake ..

build: do-cmake
	cd build && make -j

build-debug:
	mkdir -p build
	cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug
	cd build && make -j

build-run: build run

build-test:
	mkdir -p build
	cd build && cmake .. -Dlumidb_test=on
	cd build && make

	cd build && make test

run:
	./build/lumidb --in datas/students.in ${ARGS}

test:
	cd build && make test

e2e:
	python3 e2e/test.py

CASE = test_unit
test-case:
	# @cd build && make -j 1>/dev/null
	@cd build && make -j
	@./build/test/${CASE}

clean:
	rm -r build
