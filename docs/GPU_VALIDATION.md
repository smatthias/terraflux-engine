# Physical GPU validation

Status: **not run**. This checklist prepares hardware validation; it does not
certify a GPU or change the completion estimates.

Implementation checkpoint: `9196da2632abb408d93d7842453d9dc1a8fc48df`.
Use the isolated-worktree instructions in [ENGINE_BASELINE.md](ENGINE_BASELINE.md).
Commands below run from that worktree root in Bash on a local graphical Linux
session with an OpenGL 4.3-capable GPU. Install the baseline dependencies plus
`mesa-utils` for `glxinfo` if needed. Do not use Xvfb for hardware evidence.

## Build and identify the renderer

Use a fresh results directory for every run. Keep the same shell for all blocks.

```bash
set -euo pipefail
unset LIBGL_ALWAYS_SOFTWARE
TF_GPU_RESULTS="$(mktemp -d "${TMPDIR:-/tmp}/terraflux-gpu.XXXXXX")"
export TF_GPU_RESULTS
git rev-parse HEAD > "$TF_GPU_RESULTS/revision.txt"
git status --short > "$TF_GPU_RESULTS/worktree.txt"
uname -a > "$TF_GPU_RESULTS/system.txt"
glxinfo -B | tee "$TF_GPU_RESULTS/glxinfo.txt"
cmake -S native-compute -B build-gl -DCMAKE_BUILD_TYPE=Release -DTERRAFLUX_WITH_OPENGL=ON
cmake --build build-gl --parallel 2
ctest --test-dir build-gl --output-on-failure | tee "$TF_GPU_RESULTS/native-tests.txt"
cp build-gl/CMakeCache.txt "$TF_GPU_RESULTS/CMakeCache.txt"
```

Record GPU model, driver, OS, display/session, monitor scale, power mode and any
GPU-selection environment overrides. Confirm the renderer identifies the intended
physical GPU; llvmpipe/softpipe/software rendering is not hardware validation.
Mesa itself may be a hardware driver. Direct-rendering status alone is insufficient.
On hybrid systems, run identification and tests with the same GPU-selection settings.
Also inspect viewer startup output for renderer information when available.

## Logged smoke checks

The wrapper records both output and exit status. A failed check stops the batch;
investigate and rerun in a fresh results directory rather than recording a pass.
Generated request/response data and logs remain together.

```bash
tf_gpu_run() {
  local label="$1"
  shift
  local result=0
  "$@" > "$TF_GPU_RESULTS/$label.log" 2>&1 || result=$?
  cat "$TF_GPU_RESULTS/$label.log"
  printf '%s\t%s\n' "$label" "$result" >> "$TF_GPU_RESULTS/status.tsv"
  return "$result"
}
TF_VIEWER=./build-gl/terraflux-terrain-viewer
python3 examples/generate_wildfire_scenario.py > "$TF_GPU_RESULTS/request.json"
./build-gl/terraflux-compute-runner < "$TF_GPU_RESULTS/request.json" > "$TF_GPU_RESULTS/response.json"
tf_gpu_run offscreen ./build-gl/terraflux-offscreen-tests
tf_gpu_run terrain "$TF_VIEWER" --smoke-test
tf_gpu_run materials "$TF_VIEWER" --smoke-test --obj examples/assets/pyramid_materials.obj
tf_gpu_run mixed "$TF_VIEWER" --smoke-test --scene examples/mixed_asset_scene.json --residency-report
tf_gpu_run scene-controls "$TF_VIEWER" --smoke-test --scene-controls-test --scene examples/mixed_asset_scene.json
tf_gpu_run scene-residency "$TF_VIEWER" --smoke-test --scene-residency-test --scene examples/mixed_asset_scene.json
tf_gpu_run asset-lifecycle "$TF_VIEWER" --smoke-test --asset-lifecycle-test --scene examples/mixed_asset_scene.json
tf_gpu_run resource-recovery "$TF_VIEWER" --smoke-test --resource-recovery-test --residency-report --scene examples/mixed_asset_scene.json
tf_gpu_run resize "$TF_VIEWER" --smoke-test --offscreen --offscreen-resize-test --scene examples/mixed_asset_scene.json
for mode in direct offscreen; do
  TF_MODE_ARGS=()
  if [[ "$mode" == offscreen ]]; then TF_MODE_ARGS=(--offscreen); fi
  tf_gpu_run "tiles-$mode" "$TF_VIEWER" --smoke-test --terrain-tile-quads 8 --terrain-tile-test "${TF_MODE_ARGS[@]}"
  tf_gpu_run "residency-$mode" "$TF_VIEWER" --smoke-test --terrain-tile-quads 8 --terrain-residency --terrain-residency-test "${TF_MODE_ARGS[@]}"
  tf_gpu_run "retention-$mode" "$TF_VIEWER" --smoke-test --terrain-tile-quads 8 --terrain-retain-frames 2 --terrain-retention-test "${TF_MODE_ARGS[@]}"
  tf_gpu_run "layers-$mode" "$TF_VIEWER" --smoke-test --layers-test --request "$TF_GPU_RESULTS/request.json" --response "$TF_GPU_RESULTS/response.json" "${TF_MODE_ARGS[@]}"
  tf_gpu_run "replay-$mode" "$TF_VIEWER" --smoke-test --replay-controls-test --request "$TF_GPU_RESULTS/request.json" --response "$TF_GPU_RESULTS/response.json" "${TF_MODE_ARGS[@]}"
done
```

Acceptance: all commands exit zero; scripted checks report success; no unexpected
GL errors, pixel mismatches or crashes. Expected intentional budget rejections in
recovery tests must finish in the successful recovery path. Check these markers:

| Log | Required marker |
| --- | --- |
| tiles-direct/offscreen | Terrain tiles passed |
| residency-direct/offscreen | Terrain residency passed |
| retention-direct/offscreen | Terrain retention passed |
| layers-direct/offscreen | Layer visibility passed |
| replay-direct/offscreen | Replay controls passed |
| resize | Offscreen resize passed |
| resource-recovery | Resource recovery passed |

These commands are a representative hardware gate. The [CI workflow](../.github/workflows/ci.yml)
contains additional format, budget, persistence and negative-input cases; record
any unexecuted cases explicitly instead of claiming the full CI matrix ran locally.

## Interactive review

```bash
tf_gpu_run interactive "$TF_VIEWER" --scene examples/mixed_asset_scene.json --residency-report --profile --request "$TF_GPU_RESULTS/request.json" --response "$TF_GPU_RESULTS/response.json"
tf_gpu_run interactive-offscreen "$TF_VIEWER" --offscreen --terrain-retain-frames 2 --terrain-tile-quads 8 --scene examples/mixed_asset_scene.json --residency-report --request "$TF_GPU_RESULTS/request.json" --response "$TF_GPU_RESULTS/response.json"
```

- Inspect materials and terrain; orbit/zoom and check clipping.
- Resize and minimize/restore; verify aspect ratio, recovery and no persistent corruption.
- P/N: pause and single-step scene animation.
- Brackets/D/A: select, detach and reactivate a scene binding.
- U/R: unload and reload; terrain and wildfire remain available.
- T/F: independently toggle terrain/fire and restore both.
- Space/Home/comma/period: pause, reset and step wildfire replay.
- Record screenshots and any visual defects with exact reproduction steps.

## Repeated performance measurements

Use the same GPU, driver, display settings and power mode. Close unrelated GPU
workloads and allow temperatures to stabilize. These existing scripts use eight
warmup frames and disable VSync; each trial below measures 600 frames per variant.

```bash
for trial in 1 2 3 4 5; do
  tf_gpu_run "culling-$trial" python3 examples/benchmark_culling.py --viewer "$TF_VIEWER" --frames 600
  tf_gpu_run "instancing-$trial" python3 examples/benchmark_instancing.py --viewer "$TF_VIEWER" --frames 600
done
printf 'Results: %s\n' "$TF_GPU_RESULTS"
```

Keep all five trials. Summarize the median and range of each variant's reported
CPU-render mean and GPU clear/draw mean, plus draw counts and GPU sample counts.
These are distributions of trial means, not per-frame p95/p99 latency.
Missing/zero GPU samples make GPU timing inconclusive. The scripts always run
their baseline variant first; disclose possible ordering/thermal bias.
A slowdown is a measured result, not a functional failure. No universal FPS or
speedup threshold is established. Logical resource bytes are not physical VRAM.

## Results template

Copy into a dated report and retain the raw results directory.

| Field | Result |
| --- | --- |
| Date/operator | Pending |
| Commit and worktree changes | Pending |
| GPU / driver / renderer | Pending |
| OS / session / display / scale | Pending |
| Power mode / GPU overrides | Pending |
| Build configuration | Release, OpenGL ON; confirm |
| Native tests | Not run |
| Hardware smoke checks / required markers | Not run |
| Interactive direct / offscreen review | Not run |
| Culling trials: CPU/GPU median and range | Not run |
| Instancing trials: CPU/GPU median and range | Not run |
| GPU sample counts | Not run |
| Raw logs / screenshots | Pending |
| Skipped cases / defects / reproduction | Pending |
| Functional verdict | Not run |
| Performance conclusion | Not measured |

Mark hardware validation complete only after reviewing actual renderer identity,
exit statuses, success markers and interactive findings. Keep benchmark conclusions
separate from functional acceptance. Preserve logs outside temporary storage
before cleanup or reboot.
