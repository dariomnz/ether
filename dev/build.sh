#!/bin/bash
set -e

build_dir="./build"

cmake -B ${build_dir} \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

FLAGS=""
for arg in "$@"; do
    if [ "$arg" == "clean" ]; then
        FLAGS="--clean-first"
        break # Salir del bucle una vez encontrado
    fi
done

cmake --build ${build_dir} -j $(nproc) ${FLAGS}

./build/ether --test ./test