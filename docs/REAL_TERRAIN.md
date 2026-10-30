# Real DEM terrain

`examples/dem_to_terrain.py` converts an existing georeferenced elevation raster
(e.g. a GeoTIFF DEM) into a v2 request for the existing viewer. It does not
fetch data. Obtain a local DEM crop from your chosen elevation provider first;
an aerial image or ordinary map image does not contain elevation values.

From the repository root, with your Python environment activated:

```bash
python3 -m pip install -r examples/requirements-dem.txt
python3 examples/dem_to_terrain.py /actual/path/to/elevation.tif --output terrain-request.json
./build-gl/terraflux-compute-runner < terrain-request.json > terrain-response.json
./build-gl/terraflux-terrain-viewer --request terrain-request.json --response terrain-response.json --hide-fire
```

Replace `/actual/path/to/elevation.tif` with a real file on your computer.
The converter creates `terrain-request.json` and `terrain-request.metadata.json`.
The runner creates `terrain-response.json`; neither JSON input is shipped or
assumed to already exist. Use `&&` between commands if pasting as a single shell
workflow so a failed conversion does not launch an old request.

The output defaults to at most 256 samples per side without automatically
upsampling beyond the estimated native resolution. For more detail:

```bash
python3 examples/dem_to_terrain.py elevation.tif --max-side 512 --output terrain-request.json
```

For explicit spacing, use `--cell-size 30 --max-side 512`. An oversized extent
rejects with an error: crop it first or choose coarser spacing. At most 1000
samples per side are allowed, matching the engine's one-million-cell ceiling.
Large JSON requests and full-resolution meshes can consume substantial memory.

## Coordinates and data handling

- Requires a source CRS. Reprojects into the local UTM zone selected from the
  extent centre, with square cells in metres. Limited to local crops no wider
  or taller than six degrees, within 80°S–84°N. Use an appropriate local source
  and crop; this is not a global terrain streaming system.
- Nearest-neighbour resampling preserves source heights and NoData masks.
  Omitted cells leave holes; no artificial elevation filling is performed.
  Conversion rejects rasters without any complete valid 2x2 block to mesh.
- North is positive grid y: source raster rows are reversed for engine cells.
  Vertex locations correspond to target pixel centres. Metadata records the
  target raster transform and CRS, allowing local grid cells to be geolocated.
- Band 1 is used by default; override with `--band N`. Applies band scale and
  offset. Heights are assumed metres after that scaling. If they are feet,
  supply `--vertical-unit feet`. Check the source's vertical datum and units;
  no vertical datum transformation is performed.
- Fuel is zero and the request runs one step, producing an empty fire frame.
  This is terrain visualization from real elevations. Fuel, moisture and weather
  are not inferred from elevation, and the result is not a real wildfire scenario.
- The metadata sidecar records the source path, CRS, resampling, grid spacing,
  valid/missing counts and height range. The viewer uses only the request and
  response files, not this sidecar.

## Verification

```bash
python3 -m unittest discover -s examples -p 'test_dem_to_terrain.py' -v
```

Tests use generated GeoTIFF fixtures to check north/south orientation, NoData
holes, band scale/offset, feet conversion, geographic-to-metric reprojection,
bounded output and invalid-input rejection. No provider DEM is bundled.
