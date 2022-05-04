MAKEFLAGS += --always-make

build:
	ninja -C ./build


test: build
	py ./test/runtest.py 'build/test/main-test lua/ispawn-rs-test.lua' -t 20 -p 4

valgrind-test: build
	py ./test/runtest.py 'valgrind --leak-check=full build/test/main-test lua/ispawn-rs-test.lua' -t 20 -p 4


clean-log:
	rm ./build/logs/*

