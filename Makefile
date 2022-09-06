.PHONY: run_debug run_release

.DEFAULT_GOAL := run_release

debug: main.cpp
	emcc -c main.cpp -g -O0 -o main.o -I${HOME}/arrow/cpp/src/ -I${HOME}/arrow/cpp/wasm_debug/src/
	emcc main.o -g ${HOME}/arrow/cpp/wasm_debug/debug/libarrow.a ${HOME}/arrow/cpp/wasm_debug/debug/libarrow_bundled_dependencies.a -o main_debug.html -fwasm-exceptions -sLLD_REPORT_UNDEFINED -sALLOW_MEMORY_GROWTH

release: main.cpp
	emcc -c main.cpp -O3 -o main.o -I${HOME}/arrow/cpp/src/ -I${HOME}/arrow/cpp/wasm/src/
	emcc main.o ${HOME}/arrow/cpp/wasm/release/libarrow.a ${HOME}/arrow/cpp/wasm/release/libarrow_bundled_dependencies.a -o main_release.html -fwasm-exceptions -sLLD_REPORT_UNDEFINED -sALLOW_MEMORY_GROWTH

run_debug: debug
	emrun main_debug.html

run_release: release
	emrun main_release.html
