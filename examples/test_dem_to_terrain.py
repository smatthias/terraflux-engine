"""Run: python3 -m unittest discover -s examples -p 'test_dem_to_terrain.py' -v"""
import tempfile
import unittest
from pathlib import Path
import numpy as np
import rasterio
from rasterio.transform import from_origin
from dem_to_terrain import convert


class DemConversionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'dem.tif'

    def write_dem(self, values, crs='EPSG:32633', transform=None, scale=1, offset=0):
        with rasterio.open(self.path, 'w', driver='GTiff', width=values.shape[1],
                           height=values.shape[0], count=1, dtype='float32', crs=crs,
                           transform=transform or from_origin(500000, 5800000, 30, 30),
                           nodata=-9999) as dst:
            dst.write(values.astype('float32'), 1)
            dst.scales = [scale]
            dst.offsets = [offset]

    def test_orientation_holes_scale_and_native_contract(self):
        self.write_dem(np.array([[10, 20, 30], [40, 50, -9999], [70, 80, 90]]), scale=2, offset=1)
        request, metadata = convert(self.path, cell_size=30)
        cells = {(c['x'], c['y']): c for c in request['cells']}
        self.assertEqual(cells[0, 2]['elevation_meters'], 21)
        self.assertEqual(cells[0, 0]['elevation_meters'], 141)
        self.assertNotIn((2, 1), cells)
        self.assertEqual(metadata['omitted_cells'], 1)
        self.assertEqual(metadata['width'], 3)
        self.assertTrue(all(c['fuel_load'] == 0 for c in cells.values()))

    def test_geographic_dem_is_reprojected_to_metric_square_grid(self):
        self.write_dem(np.full((30, 30), 100), crs='EPSG:4326',
                       transform=from_origin(13.4, 52.5, 0.001, 0.001))
        request, metadata = convert(self.path, max_side=20)
        self.assertEqual(metadata['grid_crs'], 'EPSG:32633')
        self.assertLessEqual(max(metadata['width'], metadata['height']), 20)
        self.assertGreater(request['cell_size_meters'], 100)
        self.assertTrue(all(c['elevation_meters'] == 100 for c in request['cells']))

    def test_invalid_inputs_reject(self):
        self.write_dem(np.full((3, 3), -9999))
        with self.assertRaisesRegex(ValueError, '2x2'):
            convert(self.path)
        self.write_dem(np.ones((3, 3)))
        for options in ({'max_side': 1001}, {'cell_size': float('nan')}, {'band': 2},
                        {'cell_size': 0.001, 'max_side': 2}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                convert(self.path, **options)
        self.write_dem(np.ones((3, 3)), crs=None)
        with self.assertRaisesRegex(ValueError, 'CRS'):
            convert(self.path)

    def test_feet_conversion(self):
        self.write_dem(np.full((3, 3), 100))
        request, _ = convert(self.path, cell_size=30, vertical_unit='feet')
        self.assertAlmostEqual(request['cells'][0]['elevation_meters'], 30.48)


if __name__ == '__main__':
    unittest.main()
