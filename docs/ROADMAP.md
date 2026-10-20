# TerraFlux Engine — Development Roadmap

## Scope
TerraFlux is an independent C++20 / Python spatial simulation engine. It is not the private GeoSentinel3D application. The current baseline includes deterministic wildfire grid propagation, versioned contracts, reference fixtures, native/Python parity verification and CI.

## Planning and time accounting
All hour figures below are **estimates of future engineering effort**, not time already worked, approved, or payable. A successful Git commit, an estimate, or historical work is **not proof of time spent**. Only work performed during an approved engagement and recorded through the program's required mechanism can potentially count for payment. Check current Alignerr CC-CLI project eligibility, assistant/tool rules, start authorization, time tracking, and any session/project caps before beginning compensated work.

## Phase 1 — Usable simulation foundation (~20 hours)
| Work package | Estimate | Deliverable |
| --- | ---: | --- |
| Scenario configuration and validation | 5h | Configurable ignition, fuel, terrain, wind and deterministic seeds with schema validation |
| Simulation timeline and replay | 4h | Frame stepping, playback and deterministic replay APIs |
| Interactive wildfire grid viewer | 5h | Self-contained local demo with frames, legend, playback controls and no private assets |
| Scenario comparison | 3h | Comparison report for two reproducible simulation scenarios |
| Tests, examples and docs | 3h | Edge-case tests, reproducible quick-start examples, CI coverage and documentation |

### Definition of done
Each issue requires reviewed source changes, repeatable local run instructions, automated tests where relevant, and passing GitHub Actions. Avoid claims of scientific predictive accuracy: demo wildfire spread models are simplified and must be labeled accordingly.

## Phase 2 — Extension and robustness (~20 hours)
- Stable simulation plugin interface for additional hazard models
- Richer input grids and scenario loading
- Error handling, validation, and compatibility guarantees
- Packaging and developer experience improvements

## Phase 3 — Advanced scenarios and visualization (~20 hours)
- Multi-scenario experiments and timeline comparison
- Standalone visualization enhancements
- Extensible spatial overlays based on openly licensed or synthetic datasets
- Reproducible demo datasets and documentation

## Phase 4 — Performance and reliability (~20 hours)
- Benchmarks with disclosed environments and deterministic fixture sets
- Profiling and C++ memory/performance improvements
- Cross-platform build matrix and regression tests
- Release automation, API stability review and documentation

## Evidence of actual work (once authorized)
1. Confirm program approval and time-tracking requirements before beginning an eligible session.
2. Record session start/end and task reference in the approved tracker; do not estimate retrospectively for payment.
3. Link issues to branches and pull requests and document tested deliverables.
4. Keep assistant interactions and evaluation artifacts only as permitted by the program. Never expose private customer, GeoSentinel3D, or personal data.
5. Preserve a clear distinction between existing historical commits and new work done within authorized sessions.

## Publication gate
Repository stays private until owner reviews code rights, Git history and secret scans, third-party licensing, documentation and output assets. Source extraction alone does not establish complete publication clearance.

## Relationship to GeoSentinel3D
TerraFlux can later be integrated as an independent dependency. GeoSentinel3D proprietary backend, frontend, customer/data infrastructure, CryEngine adapter and provider credentials are explicitly out of scope.
