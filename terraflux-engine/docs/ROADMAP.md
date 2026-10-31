# TerraFlux Engine — Long-Term Engineering Roadmap

## Project boundary
TerraFlux is an independent C++20/Python spatial simulation engine. It is **not** the private GeoSentinel3D application. Current baseline: deterministic grid wildfire propagation, versioned contracts, native C++ runner, Python reference implementation, parity fixtures and CI. Proposed features below are **not implemented yet**.

## Estimated future work: ~220 engineering hours

These are **planning estimates**, not verified time already spent, paid time, a minimum, or an Alignerr allowance. Estimates can change after design and technical discovery. We do not know whether Alignerr has approved this project or whether the program sets any session or project cap. Approval, time-tracking and permitted tooling must be confirmed before counting eligible work. Git commits are evidence of changes, not evidence of hours.

### Phase 1 — Demonstrable foundation (~20h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#1](../issues/1) | Scenario configuration and input validation | 5h |
| [#2](../issues/2) | Simulation timeline and deterministic replay | 4h |
| [#3](../issues/3) | Standalone interactive wildfire grid demo | 5h |
| [#4](../issues/4) | Reproducible scenario comparison | 3h |
| [#5](../issues/5) | Expanded tests, examples and documentation | 3h |

### Phase 2 — Reusable computation architecture (~38h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#6](../issues/6) | Sparse/dense spatial grids, tiling and coordinate metadata | 18h |
| [#9](../issues/9) | Modular simulation solver API and update strategies | 20h |

### Phase 3 — Richer modeling and uncertainty (~42h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#7](../issues/7) | Wind, slope, fuel and moisture wildfire model extensions | 22h |
| [#8](../issues/8) | Seeded ensembles and uncertainty summaries | 20h |

### Phase 4 — Geospatial interchange and API (~36h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#10](../issues/10) | Coordinate systems, georeferenced grids and raster interchange | 20h |
| [#11](../issues/11) | Headless CLI, scenario runner and structured output | 16h |

### Phase 5 — Visualization (~22h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#12](../issues/12) | Standalone visualization architecture and spatial layer composition | 22h |

### Phase 6 — Performance and concurrency (~42h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#13](../issues/13) | Benchmarks, profiling and native performance budgets | 20h |
| [#14](../issues/14) | Parallel execution, cancellation and job orchestration | 22h |

### Phase 7 — Developer SDK and release readiness (~20h)

| Issue | Deliverable | Est. |
| --- | --- | ---: |
| [#15](../issues/15) | Stable native/Python packages, versioning and release CI | 20h |

**Total: 20 + 38 + 42 + 36 + 22 + 42 + 20 = 220 hours estimated.**

## Further research backlog (unestimated)

Potential longer-term directions, subject to scope and feasibility:
- Flood and terrain-flow simulation kernels with validated reference datasets
- Heat and atmospheric transport models, if scientifically justified
- Agent-based movement and evacuation simulation
- Data assimilation and calibrated model parameterization
- GPU compute backends and distributed simulation
- General scene export and visualization connectors
- Numerical validation against published benchmarks

Do not treat these as implemented features or assign hours until requirements are defined. Do not claim real-world hazard forecasting accuracy from an illustrative grid model.

## Definition of done
Every issue requires a reproducible deliverable, clear model assumptions, reviewed source changes, repeatable instructions and appropriate automated tests. Preserve deterministic regression and native/Python parity unless a deliberate, versioned contract change is documented.

## Evidence of actual work, if authorized
1. Confirm Alignerr program approval, hourly payment rules, allowed coding assistant/tool requirements and any limits.
2. Use the platform's required timer or tracker for real session start/end. Never backfill speculative hours.
3. Associate task sessions with issue numbers, change sets, test results and PRs where permitted.
4. Keep old preserved commits clearly distinct from new work completed after authorization.
5. Do not disclose private user data, GeoSentinel3D proprietary material or confidential program evaluation data.

## Publication gate
Keep the repository private until reviewing ownership, third-party rights, historical Git objects, secrets and security scans, generated assets and documentation. Successful automated tests alone do not imply that the code is cleared for public release.

## Independence from GeoSentinel3D
TerraFlux may later serve as an optional reusable dependency, but proprietary GeoSentinel3D backend, frontend, CryEngine adapter, operational data, internal infrastructure and credentials are excluded.
