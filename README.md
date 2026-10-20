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
```

The executable is located at `build/terraflux-compute-runner`.

## Python setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r intelligence/requirements.txt
```

## Run tests

```bash
PYTHONPATH=intelligence \
  python -m unittest discover -s intelligence/tests -v
```

## Verify C++/Python parity

```bash
PYTHONPATH=intelligence \
  python intelligence/scripts/verify_native_parity.py \
    --runner build/terraflux-compute-runner \
    --fixtures intelligence/tests/fixtures/grid_spread_cases.json
```

## Simulation contract

Current contract version: `terraflux.compute/v1`.

## Project status

TerraFlux is an early-stage simulation engine currently focused on
deterministic grid-based wildfire propagation. Additional environmental
models and visualization capabilities may be added later.

## License

MIT. See [LICENSE](LICENSE).

