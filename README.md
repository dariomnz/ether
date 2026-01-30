<p align="center">
  <img src=".vscode/extensions/ether-vscode/icons/ether.svg" width="128" />
</p>

# Ether Programming Language

Ether is a custom, high-performance programming language designed with built-in concurrency features and low-level system control. It compiles to a custom stack-based bytecode and executes on a specialized Virtual Machine (VM) that uses `io_uring` for efficient asynchronous I/O.

## Features

- **Concurrency**: Native support for lightweight coroutines using `spawn`, `yield`, and `await`.
- **System Programming**: direct memory access with pointers (`ptr(T)`), structs, and manual memory management (`malloc`/`free`).
- **Modern Tooling**: Includes a fully functional Language Server (LSP) implementing diagnostics, go-to-definition, and semantic highlighting.
- **Efficient VM**: A stack-based virtual machine designed for speed and asynchronous operations.
- **Clean Syntax**: C-like syntax that is familiar and easy to write.

## Requirements

- C++20 compatible compiler (GCC or Clang)
- CMake (version 3.10 or higher)
- `liburing` (required for the VM's async I/O)

## Building the Project

You can build the project using standard CMake commands:

```bash
mkdir -p build
cd build
cmake ..
make
```

Alternatively, you can use the provided build script:

```bash
./dev/build.sh
```

## Usage

### Running a Program
To compile and run an Ether source file:
```bash
./build/ether examples/hello.eth
```

### Viewing Bytecode (IR)
To inspect the generated Intermediate Representation (IR):
```bash
./build/ether examples/hello.eth --dump-ir
```

### Running Tests
To run the project's test suite:
```bash
./build/ether --test test/
```

### Language Server
To start the Language Server (typically used by an editor extension):
```bash
./build/ether --lsp
```

## Language Example

Here is a simple example demonstrating structs, functions, and coroutines:

```cpp
#include "std/io.eth"

struct Point {
    i32 x;
    i32 y;
}

Point make_point(i32 x, i32 y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

i32 main() {
    // Structural types and return values
    Point p = make_point(10, 20);
    printf("Point: %d, %d\n", p.x, p.y);

    // Spawning a coroutine
    spawn worker(p);
    
    return 0;
}

void worker(Point p) {
    printf("Worker processing point: %d\n", p.x + p.y);
    yield; // Yield execution back to the scheduler
    printf("Worker finished\n");
}
```

## Project Structure

- `src/`: Core source code
  - `lexer/`, `parser/`: Frontend analysis
  - `sema/`: Semantic analysis and type checking
  - `ir/`: Intermediate Representation generation and disassembly
  - `vm/`: The virtual machine and runtime
  - `lsp/`: Language Server Protocol implementation
- `test/`: Integration and unit tests
- `std/`: Standard library include files