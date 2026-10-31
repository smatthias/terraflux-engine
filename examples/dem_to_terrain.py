#!/usr/bin/env python3
"""Convert a georeferenced DEM to TerraFlux v2 terrain request JSON.

Install optional dependencies: python3 -m pip install -r examples/requirements-dem.txt
Elevations must be metres unless --vertical-unit feet is supplied. Raster band
scale/offset metadata is applied before unit conversion. NoData remains holes.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys


def convert(source: Path, *, max_side: int = 256, cell_size: float | None = None,
            band: int = 1, vertical_unit: str = "metres") -> tuple[dict, dict]:
    import numpy as np
    import rasterio
    from rasterio.transform import from_origin
    from rasterio.warp import Resampling, calculate_default_transform, reproject, transform_bounds

    if not 2 <= max_side <= 1000:
        raise ValueError("max-side must be between 2 and 1000")
    if cell_size is not None and (not math.isfinite(cell_size) or not 0 < cell_size <= 10000):
        raise ValueError("cell-size must be positive and at most 10000 metres")
    if vertical_unit not in ("metres", "feet"):
        raise ValueError("vertical-unit must be metres or feet")
    with rasterio.open(source) as src:
        if src.crs is None:
            raise ValueError("DEM has no CRS; assign its correct CRS before conversion")
        if not 1 <= band <= src.count:
            raise ValueError("band is outside the DEM band range")
        west, south, east, north = transform_bounds(src.crs, "EPSG:4326", *src.bounds)
        lon, lat = (west + east) / 2, (south + north) / 2
        if not (-80 <= south <= north <= 84) or east - west > 6 or north - south > 6:
            raise ValueError("Use a local DEM crop (within 6 degrees, inside UTM latitude limits)")
        zone = min(60, max(1, int((lon + 180) // 6) + 1))
        crs = rasterio.crs.CRS.from_epsg((32600 if lat >= 0 else 32700) + zone)
        left, bottom, right, top = transform_bounds(src.crs, crs, *src.bounds)
        # Automatic sizing caps both axes. Explicit spacing rejects oversized
        # inputs rather than allocating an unbounded array or silently coarsening.
        native_transform, _, _ = calculate_default_transform(
            src.crs, crs, src.width, src.height, *src.bounds)
        spacing = cell_size or max(abs(native_transform.a), abs(native_transform.e),
                                   max(right - left, top - bottom) / max_side)
        if not math.isfinite(spacing) or not 0 < spacing <= 10000:
            raise ValueError("Resulting spacing must be in (0, 10000] metres; use a smaller crop")
        width = max(1, math.ceil((right - left) / spacing - 1e-10))
        height = max(1, math.ceil((top - bottom) / spacing - 1e-10))
        if width > max_side or height > max_side:
            raise ValueError("Grid exceeds max-side; increase cell-size or crop the DEM")
        if min(width, height) < 2:
            raise ValueError("DEM must cover at least two rows and columns")
        affine = from_origin(left, top, spacing, spacing)
        elevations = np.full((height, width), np.nan, dtype="float64")
        reproject(source=rasterio.band(src, band), destination=elevations,
                  src_transform=src.transform, src_crs=src.crs, src_nodata=src.nodatavals[band - 1],
                  dst_transform=affine, dst_crs=crs, dst_nodata=np.nan,
                  resampling=Resampling.nearest, num_threads=1)
        elevations = elevations * src.scales[band - 1] + src.offsets[band - 1]
        if vertical_unit == "feet":
            elevations *= 0.3048
        valid = np.isfinite(elevations)
        if not (valid[:-1, :-1] & valid[1:, :-1] & valid[:-1, 1:] & valid[1:, 1:]).any():
            raise ValueError("DEM has no complete 2x2 block of valid elevations to mesh")
        cells = [dict(x=int(col), y=int(height - 1 - row),
                      elevation_meters=float(elevations[row, col]), fuel_load=0,
                      fuel_moisture=1, burn_susceptibility=0)
                 for row, col in zip(*np.nonzero(valid))]
        request = dict(contract_version="terraflux.compute/v2", grid_key="dem-terrain",
                       cell_size_meters=spacing, cells=cells,
                       ignition=dict(x=cells[0]["x"], y=cells[0]["y"]), steps=1,
                       wind_speed_kmh=0, wind_direction_degrees=0, spread_threshold=1)
        metadata = dict(source=str(source), source_crs=str(src.crs), band=band,
                        vertical_unit=vertical_unit, band_scale=src.scales[band - 1],
                        band_offset=src.offsets[band - 1], grid_crs=str(crs),
                        width=width, height=height, cell_size_meters=spacing,
                        raster_transform=list(affine)[:6], valid_cells=len(cells),
                        omitted_cells=int(width * height - len(cells)),
                        min_elevation_meters=float(elevations[valid].min()),
                        max_elevation_meters=float(elevations[valid].max()),
                        resampling="nearest", grid_y="north; raster row = height - 1 - y",
                        note="Local UTM grid. Elevation units/datum must be verified against source metadata. Fuel is zero; terrain preview only.")
        return request, metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dem", type=Path)
    parser.add_argument("--output", type=Path, default=Path("terrain-request.json"))
    parser.add_argument("--max-side", type=int, default=256)
    parser.add_argument("--cell-size", type=float, help="Explicit square grid spacing in metres")
    parser.add_argument("--band", type=int, default=1)
    parser.add_argument("--vertical-unit", choices=("metres", "feet"), default="metres")
    args = parser.parse_args()
    metadata_path = args.output.with_suffix(".metadata.json")
    try:
        if args.output.resolve() == args.dem.resolve() or metadata_path.resolve() == args.dem.resolve():
            raise ValueError("Output must not overwrite the source DEM")
        request, metadata = convert(args.dem, max_side=args.max_side, cell_size=args.cell_size,
                                    band=args.band, vertical_unit=args.vertical_unit)
        args.output.write_text(json.dumps(request, allow_nan=False, separators=(",", ":")) + "\n")
        metadata_path.write_text(json.dumps(metadata, allow_nan=False, indent=2) + "\n")
    except ImportError:
        parser.exit(2, "Install DEM dependencies: python3 -m pip install -r examples/requirements-dem.txt\n")
    except (ValueError, OSError) as exc:
        parser.exit(2, f"DEM conversion failed: {exc}\n")
    print(f"Wrote {args.output}: {metadata['valid_cells']} cells, "
          f"{metadata['cell_size_meters']:.3f} m spacing; metadata: {metadata_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
