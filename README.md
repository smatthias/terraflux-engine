# TerraFlux Engine

An open-source spatial simulation engine for dynamic environments,
built with C++20 and Python.

TerraFlux provides a deterministic grid-based simulation foundation
for environmental modeling, spatial computation, and future digital
twin applications.

## Current capabilities

- Native C++20 grid-based wildfire spread simulation
- Python reference implementation
- Versioned simulation contracts
- Deterministic simulation fixtures
- C++/Python parity verification

## Requirements

- C++20-compatible compiler
- CMake 3.16 or newer
- Python 3.10 or newer
- Pydantic 2.x

## Build the native engine

```bash
cmake -S native-compute -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
