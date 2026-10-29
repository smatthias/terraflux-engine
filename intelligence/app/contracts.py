"""Versioned compute contracts shared by the Python reference engine and native adapters."""
from typing import Literal

from pydantic import BaseModel, Field, field_validator

# Contract history
#   v1  fuel / moisture / susceptibility / wind. `elevation_meters` is accepted but has no effect.
#   v2  adds `cell_size_meters` and the terrain (slope) term. Elevation now changes results.
# Both versions remain accepted. A request is evaluated under the semantics of the version it
# declares, so stored v1 requests replay bit-for-bit even if they carry elevation data.
CONTRACT_VERSION_V1 = "terraflux.compute/v1"
CONTRACT_VERSION_V2 = "terraflux.compute/v2"
CONTRACT_VERSION = CONTRACT_VERSION_V2
SUPPORTED_CONTRACT_VERSIONS = (CONTRACT_VERSION_V1, CONTRACT_VERSION_V2)
ContractVersion = Literal["terraflux.compute/v1", "terraflux.compute/v2"]


def terrain_enabled(contract_version: str) -> bool:
    """Terrain was introduced in v2; v1 keeps the legacy, elevation-blind behaviour."""
    return contract_version != CONTRACT_VERSION_V1


class GridCell(BaseModel):
    x: int
    y: int
    fuel_load: float = Field(ge=0)
    fuel_moisture: float = Field(ge=0, le=1)
    burn_susceptibility: float = Field(ge=0, le=1)
    elevation_meters: float | None = Field(default=None, allow_inf_nan=False)
    population: float | None = Field(default=None, ge=0)


class IgnitionCell(BaseModel):
    x: int
    y: int


class SpreadRequest(BaseModel):
    # New requests default to the current contract. Wire clients that still send v1 get v1 semantics.
    contract_version: ContractVersion = CONTRACT_VERSION
    grid_key: str = Field(min_length=1, max_length=128)
    # Raised from 100k after measuring: 1M cells simulate in ~30 s in the Python reference and a few
    # seconds natively; memory is dominated by the per-frame cumulative burned lists (steps x burned).
    cells: list[GridCell] = Field(min_length=1, max_length=1_000_000)
    ignition: IgnitionCell
    steps: int = Field(default=12, ge=1, le=720)
    wind_speed_kmh: float = Field(default=0, ge=0, le=300)
    # Compass degrees, clockwise from +y (north): 0 = north, 90 = east (+x), 180 = south, 270 = west.
    #   v2: meteorological convention - the direction the wind blows FROM (as in weather data).
    #   v1: frozen legacy meaning - the direction the wind blows TOWARD.
    # A v1 request cannot be migrated to v2 by changing the version string alone unless its wind
    # angles are rotated by 180 degrees (or were meteorological all along, in which case v1 was
    # pushing the fire the wrong way).
    wind_direction_degrees: float = Field(default=0, ge=0, lt=360)
    spread_threshold: float = Field(default=0.5, ge=0, le=1)
    # Horizontal distance between the centres of orthogonally adjacent cells. Only used to
    # turn `elevation_meters` differences into slope angles; has no effect when elevation is
    # absent. Default matches common 30 m DEM products (SRTM 1", Copernicus GLO-30).
    cell_size_meters: float = Field(default=30.0, gt=0, le=10_000)

    @field_validator("cells")
    @classmethod
    def unique_coordinates(cls, cells: list[GridCell]) -> list[GridCell]:
        if len({(cell.x, cell.y) for cell in cells}) != len(cells):
            raise ValueError("cells must have unique x,y coordinates")
        return cells


class SpreadFrame(BaseModel):
    step: int
    active_cells: list[tuple[int, int]]
    burned_cells: list[tuple[int, int]]
    active_count: int
    burned_count: int


class SpreadResponse(BaseModel):
    # Echoes the version the request was evaluated under.
    contract_version: ContractVersion = CONTRACT_VERSION
    engine: str
    deterministic: bool = True
    grid_key: str
    frames: list[SpreadFrame]
    metrics: dict[str, int | float]
