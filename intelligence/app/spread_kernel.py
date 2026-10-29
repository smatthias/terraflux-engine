"""Deterministic reference implementation. Native execution must preserve this contract."""
from math import atan, atan2, cos, pi, sqrt
from .contracts import GridCell, SpreadFrame, SpreadRequest, SpreadResponse, terrain_enabled

# Environmental drivers (wind and terrain). Constants are mirrored in
# native-compute/include/terraflux/contracts.hpp; keep them in sync.
#
# The score is a bounded additive "spread propensity" compared against a threshold. As in
# Rothermel's R = R0 * (1 + phi_wind + phi_slope) - and FARSITE's vector combination of the two -
# wind and slope enter as separate bounded terms, each projected onto the direction of spread:
#
#   wind_cos   = -cos(bearing - wind_direction)         -1 head wind, 0 cross wind, +1 tail wind
#                (v2: wind_direction is METEOROLOGICAL, i.e. where the wind blows FROM; the wind pushes
#                 the fire toward wind_direction + 180 deg, and cos(x - 180deg) == -cos(x).
#                 v1 interpreted the field as the direction the wind blows TOWARD; that is frozen.)
#   bearing    = compass bearing from the burning cell to the target: 0 = +y (north), 90 = +x
#                (east), 180 = -y, 270 = -x. The grid's +y axis is assumed to point north.
#   tail_drive = wind_norm * max( wind_cos, 0)           in [0, 1]
#   head_drive = wind_norm * max(-wind_cos, 0)           in [0, 1]
#   wind       = tail_drive * WIND_WEIGHT - head_drive * WIND_HEADWIND_WEIGHT
#   uphill     = +SLOPE_UPHILL_WEIGHT   * clamp( angle / 30, 0, 1)
#   downhill   = -SLOPE_DOWNHILL_WEIGHT * clamp(-angle / 30, 0, 1) * (1 - tail_drive)
#
# Rationale:
# * Wind is the dominant driver on open ground; at saturation it outweighs the steepest slope.
# * A head wind pushes the flames away from the unburnt fuel (a "backing fire"). Backing fires
#   are slower than calm fires but keep spreading in continuous fuel, so the penalty is modest.
# * A cross wind has no component along the spread path and therefore neither helps nor hinders
#   that step. In particular a wind blowing along the contour leaves the uphill preheating bonus
#   fully intact and gives no relief to the downhill penalty. (v1's unsigned alignment treated a
#   cross wind as half a tail wind; v2 uses the signed projection.)
# * Uphill: fire climbs fast because flames tilt into the slope and preheat the fuel above, which
#   then ignites more easily. The field rule of thumb is a doubling of spread rate per ~10 degrees;
#   expressed here linearly in angle (its log-space form) and saturating at 30 degrees. Wind never
#   reduces this bonus; a head wind competes with it additively (steep slope beats a gale, gentle
#   slope loses to it).
# * Downhill is NOT the mirror image. Published treatments range from no penalty at all (Rothermel)
#   to symmetric (McArthur); Sullivan et al. 2014 found the real reduction to be weaker than the
#   mirror. We use half the uphill weight, scaled by (1 - tail_drive): a tail wind tilts the flames
#   back into the fuel and drives fire downhill at essentially full strength.
# * Flat terrain or missing elevation contributes exactly 0.0.
# * v1 semantics are frozen: unsigned alignment (cos + 1) / 2, weight WIND_WEIGHT_V1, no terrain.
WIND_WEIGHT_V1 = 0.15
WIND_WEIGHT = 0.25
WIND_HEADWIND_WEIGHT = 0.10
SLOPE_UPHILL_WEIGHT = 0.15
SLOPE_DOWNHILL_WEIGHT = 0.075
SLOPE_SATURATION_DEGREES = 30.0
# Elevation voids are filled in-engine (v2) from the nearest known cell along each of the 8 compass
# rays, inverse-distance weighted - the same scheme as GDAL's FillNodata. The nearest hits are found
# with one linear sweep per direction, so the cost is O(cells) however far away the hits are, and
# the search limit below is a quality bound ("do not trust a hit further than this"), not a cost
# bound. A void with no known cell within reach along any ray stays void (flat fallback) and is
# visible in metrics.cells_with_filled_elevation.
ELEVATION_FILL_MAX_SEARCH_CELLS = 1024
# Dense line sweeps need a bounding-box array; for grids much sparser than their bounding box the
# per-void ray cast (identical results, cheap when most probes hit absent cells) is used instead.
_DENSE_FILL_MAX_AREA_FACTOR = 4

NEIGHBOURS = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))
Key = tuple[int, int]


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def fill_elevation_voids(elevations: dict[Key, float | None], blockers: frozenset[Key] | set[Key] = frozenset()) -> dict[Key, float]:
    """Estimate elevations for void cells (``None``) from the known ones.

    For each void, the first cell with a known elevation along each of the 8 directions is found
    (rays pass through other voids and stop at grid holes/edges, at zero-fuel voids, or after
    ELEVATION_FILL_MAX_SEARCH_CELLS steps). The hits are combined by inverse distance. This is
    exact linear interpolation across 1-D gaps and exact for isolated voids on a planar slope.
    Voids are estimated from the *original* known cells only, so the result does not depend on
    iteration order. Returns only the newly filled cells.

    ``blockers`` are zero-fuel cells (rivers, rock, roads). A blocker *without* elevation ends a
    ray like a grid hole: the far bank of an unmeasured river says nothing reliable about the near
    bank, and letting rays cross it pulled bank cells toward the opposite side, inventing slopes
    along the shore and into any gap in the line. A blocker *with* a measured elevation is a
    legitimate hit. Blocker voids themselves are not filled: their elevation is never read.

    Two implementations with identical results: a dense line sweep (O(cells)) when the grid fills
    its bounding box reasonably, else a per-void ray cast.
    """
    if not elevations:
        return {}
    xs = [x for x, _ in elevations]
    ys = [y for _, y in elevations]
    width = max(xs) - min(xs) + 1
    height = max(ys) - min(ys) + 1
    if width * height <= _DENSE_FILL_MAX_AREA_FACTOR * len(elevations):
        return _fill_by_line_sweep(elevations, blockers, min(xs), min(ys), width, height)
    return _fill_by_ray_cast(elevations, blockers)


def _fill_by_ray_cast(elevations: dict[Key, float | None], blockers) -> dict[Key, float]:
    filled: dict[Key, float] = {}
    for (x, y), z in elevations.items():
        if z is not None or (x, y) in blockers:
            continue
        numerator = 0.0
        denominator = 0.0
        for dx, dy in NEIGHBOURS:
            length = sqrt(2.0) if dx and dy else 1.0
            for k in range(1, ELEVATION_FILL_MAX_SEARCH_CELLS + 1):
                probe = (x + dx * k, y + dy * k)
                if probe not in elevations:
                    break
                value = elevations[probe]
                if value is None:
                    if probe in blockers:
                        break
                    continue
                weight = 1 / (k * length)
                numerator += weight * value
                denominator += weight
                break
        if denominator > 0:
            filled[(x, y)] = numerator / denominator
    return filled


def _fill_by_line_sweep(elevations, blockers, min_x: int, min_y: int, width: int, height: int) -> dict[Key, float]:
    """Same hits and arithmetic as the ray cast, found by sweeping each direction once.

    Processing cells so that ``v + d`` is visited before ``v``, the nearest hit of ``v`` along
    ``d`` is either ``v + d`` itself (known) or the hit of ``v + d`` one step further away.
    """
    size = width * height
    present = [False] * size
    known: list[float | None] = [None] * size
    blocked = [False] * size
    for (x, y), z in elevations.items():
        i = (x - min_x) * height + (y - min_y)
        present[i] = True
        known[i] = z
        blocked[i] = (x, y) in blockers
    numerator = [0.0] * size
    denominator = [0.0] * size
    for dx, dy in NEIGHBOURS:  # direction order fixes the floating-point summation order
        hit_k = [0] * size  # per direction: a skipped cell must not keep a stale hit
        hit_z = [0.0] * size
        length = sqrt(2.0) if dx and dy else 1.0
        x_range = range(width - 1, -1, -1) if dx > 0 else range(width)
        y_range = range(height - 1, -1, -1) if dy > 0 else range(height)
        for xi in x_range:
            ux = xi + dx
            if not 0 <= ux < width:
                continue
            for yi in y_range:
                i = xi * height + yi
                if not present[i] or known[i] is not None or blocked[i]:
                    hit_k[i] = 0
                    continue
                uy = yi + dy
                if not 0 <= uy < height:
                    hit_k[i] = 0
                    continue
                u = ux * height + uy
                if not present[u]:
                    hit_k[i] = 0
                    continue
                if known[u] is not None:
                    k, z = 1, known[u]
                elif blocked[u] or hit_k[u] == 0 or hit_k[u] + 1 > ELEVATION_FILL_MAX_SEARCH_CELLS:
                    hit_k[i] = 0
                    continue
                else:
                    k, z = hit_k[u] + 1, hit_z[u]
                hit_k[i] = k
                hit_z[i] = z
                weight = 1 / (k * length)
                numerator[i] += weight * z
                denominator[i] += weight
    filled: dict[Key, float] = {}
    for (x, y), z in elevations.items():
        if z is not None or (x, y) in blockers:
            continue
        i = (x - min_x) * height + (y - min_y)
        if denominator[i] > 0:
            filled[(x, y)] = numerator[i] / denominator[i]
    return filled


def slope_degrees(z_source: float | None, z_target: float | None, dx: int, dy: int, cell_size_meters: float) -> float | None:
    """Signed slope angle, in degrees, of the path from the source cell to the target cell.

    Positive means the fire would climb (target is higher), negative means it would descend.
    Returns ``None`` when either elevation is unknown, so callers can fall back to flat behaviour.
    Diagonal neighbours are sqrt(2) cell widths apart.
    """
    if z_source is None or z_target is None:
        return None
    run = cell_size_meters * (sqrt(2.0) if dx and dy else 1.0)
    rise = z_target - z_source
    return atan(rise / run) * 180 / pi


def slope_term(angle_degrees: float | None, tail_drive: float = 0.0) -> float:
    """Contribution of terrain to the spread score.

    ``tail_drive`` (0..1, wind component along the spread path) only relieves the downhill
    penalty; it never reduces the uphill bonus. Range [-SLOPE_DOWNHILL_WEIGHT, SLOPE_UPHILL_WEIGHT].
    """
    if angle_degrees is None:
        return 0.0
    steepness = _clamp(angle_degrees / SLOPE_SATURATION_DEGREES, -1, 1)
    if steepness >= 0:
        return steepness * SLOPE_UPHILL_WEIGHT
    return steepness * SLOPE_DOWNHILL_WEIGHT * (1 - tail_drive)


def wind_term(wind_norm: float, wind_cos: float) -> float:
    """v2 wind contribution: signed projection of the wind onto the spread path."""
    tail_drive = wind_norm * max(wind_cos, 0.0)
    head_drive = wind_norm * max(-wind_cos, 0.0)
    return tail_drive * WIND_WEIGHT - head_drive * WIND_HEADWIND_WEIGHT


def spread_score(request: SpreadRequest, target: GridCell, dx: int, dy: int,
                 z_source: float | None = None, z_target: float | None = None) -> float:
    """Spread propensity in [0, 1] for fire moving to the adjacent ``target`` along (dx, dy).

    ``z_source``/``z_target`` are the (possibly engine-filled) elevations; ``None`` means unknown.
    Pure arithmetic; the ignition rules (already burned, no fuel under v2, corner cutting,
    threshold) live in ``simulate``. Term order is part of the contract: the v1 partial sum must
    stay bit-identical to the original kernel, and the v2 terrain term is appended last so
    flat/missing terrain (exactly 0.0) leaves the sum untouched.
    """
    use_terrain = terrain_enabled(request.contract_version)
    bearing = (atan2(dx, dy) * 180 / pi + 360) % 360
    # Cosine between the spread bearing and the wind angle as written in the request.
    cos_toward = cos((bearing - request.wind_direction_degrees) * pi / 180)
    wind_norm = _clamp(request.wind_speed_kmh / 80, 0, 1)
    if use_terrain:
        # v2: meteorological convention, the wind blows FROM wind_direction_degrees.
        wind_cos = -cos_toward
        tail_drive = wind_norm * max(wind_cos, 0.0)
        wind = wind_term(wind_norm, wind_cos)
        terrain = slope_term(slope_degrees(z_source, z_target, dx, dy, request.cell_size_meters), tail_drive)
    else:
        # Frozen v1 expression, evaluated in the original order. v1 treated
        # wind_direction_degrees as the direction the wind blows TOWARD.
        alignment = (cos_toward + 1) / 2
        wind = wind_norm * alignment * WIND_WEIGHT_V1
        terrain = 0.0
    return _clamp(
        target.fuel_load * 0.35
        + (1 - target.fuel_moisture) * 0.25
        + target.burn_susceptibility * 0.25
        + wind
        + terrain,
        0,
        1,
    )


def simulate(request: SpreadRequest, engine: str = "python-reference") -> SpreadResponse:
    by_key = {(cell.x, cell.y): cell for cell in request.cells}
    ignition = (request.ignition.x, request.ignition.y)
    if ignition not in by_key:
        raise ValueError("ignition cell is not present in cells")

    use_terrain = terrain_enabled(request.contract_version)
    elevations: dict[Key, float | None] = {key: cell.elevation_meters for key, cell in by_key.items()}
    cells_with_elevation = sum(1 for z in elevations.values() if z is not None)
    blockers = frozenset(key for key, cell in by_key.items() if cell.fuel_load <= 0)
    filled = fill_elevation_voids(elevations, blockers) if use_terrain and cells_with_elevation else {}
    elevations.update(filled)

    def passable(key: Key) -> bool:
        cell = by_key.get(key)
        return cell is not None and cell.fuel_load > 0

    # v2: the same "no fuel, no fire" rule applies to the ignition itself. Igniting bare rock does
    # not take: nothing burns, the neighbours are never exposed, and the result is one empty frame
    # with burned_cells = 0. v1 is frozen and seeds the ignition unconditionally.
    ignition_takes = not (use_terrain and by_key[ignition].fuel_load <= 0)
    burning = {ignition} if ignition_takes else set()
    burned = set(burning)
    frames: list[SpreadFrame] = []
    for step in range(request.steps):
        next_burning: set[tuple[int, int]] = set()
        for x, y in burning:
            for dx, dy in NEIGHBOURS:
                target = (x + dx, y + dy)
                if target in burned or target not in by_key:
                    continue
                cell = by_key[target]
                if use_terrain:
                    # v2: a cell without fuel cannot ignite, whatever the wind and slope do. (The
                    # additive score would otherwise let moisture/susceptibility plus a gale on a
                    # steep climb reach the threshold with zero fuel. v1 is frozen and keeps that.)
                    if cell.fuel_load <= 0:
                        continue
                    # v2: no corner cutting. A diagonal step between two impassable cells (no fuel,
                    # or not in the grid) would let fire slip through a diagonal river or fire
                    # break that is continuous on the ground.
                    if dx and dy and not passable((x + dx, y)) and not passable((x, y + dy)):
                        continue
                score = spread_score(request, cell, dx, dy, elevations[(x, y)], elevations[target])
                if score >= request.spread_threshold:
                    next_burning.add(target)
                    burned.add(target)
        burning = next_burning
        frames.append(SpreadFrame(
            step=step, active_cells=sorted(burning), burned_cells=sorted(burned),
            active_count=len(burning), burned_count=len(burned),
        ))
        if not burning:
            break

    metrics: dict[str, int | float] = {"burned_cells": len(burned), "completed_steps": len(frames)}
    if use_terrain:
        # Elevation voids are filled in-engine where a known cell is within reach; the remainder
        # is evaluated flat per pair. Report both counts so consumers can tell how much of the
        # terrain was measured, estimated, or absent. v1 metrics are frozen.
        metrics["cells_with_elevation"] = cells_with_elevation
        metrics["cells_with_filled_elevation"] = len(filled)
    return SpreadResponse(
        contract_version=request.contract_version,
        engine=engine, grid_key=request.grid_key, frames=frames, metrics=metrics,
    )
