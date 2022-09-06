* Install Emscripten

`brew install Emscripten`

* Download toolchain file:

`cd ~ && wget https://raw.githubusercontent.com/mosra/toolchains/master/generic/Emscripten-wasm.cmake`

* Configure cmake project (on M1 Mac at least):

The CMakeLists need some massaging to get emscripten to work. The included patch
`cmake_changes.patch` is what I did to get it working. Then, build Arrow. This
repository assumes that Arrow is in `$HOME`, but the Makefile is easy to change to
point to your Arrow build directory.

```
cd ~/arrow/cpp && mkdir wasm && cd wasm

EMSCRIPTEN=/opt/homebrew/opt/emscripten/bin emcmake cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DARROW_COMPUTE=On \
    -DARROW_BUILD_STATIC=On \
    -DARROW_BUILD_SHARED=Off \
    -DEMSCRIPTEN_SYSTEM_PROCESSOR=arm \
    -DEMSCRIPTEN_ROOT_PATH=/opt/homebrew/opt/emscripten/bin \
    -DARROW_JEMALLOC=Off \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/Emscripten.cmake \
    -DARROW_SIMD_LEVEL=NONE ../

ninja
```

* Run Arrow in the browser! (sort of)

Simply run `make` in this repository. It will compile a program that generates data for
and runs TPC-H Q1 in the browser (except it'll crash for some reason, you'll get some nice
print outs though!).