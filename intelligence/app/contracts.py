"""Versioned compute contracts shared by the Python reference engine and native adapters."""
from typing import Literal

from pydantic import BaseModel, Field, field_validator

CONTRACT_VERSION = "geosentinel.compute/v1"


class GridCell(BaseModel):
    x: int
    y: int
    fuel_load: float = Field(ge=0)
    fuel_moisture: float = Field(ge=0, le=1)
    burn_susceptibility: float = Field(ge=0, le=1)
    elevation_meters: float | None = None
    population: float | None = Field(default=None, ge=0)


class IgnitionCell(BaseModel):
    x: int
    y: int


class SpreadRequest(BaseModel):
    contract_version: Literal[CONTRACT_VERSION] = CONTRACT_VERSION
    grid_key: str = Field(min_length=1, max_length=128)
    cells: list[GridCell] = Field(min_length=1, max_length=100_000)
    ignition: IgnitionCell
    steps: int = Field(default=12, ge=1, le=720)
    wind_speed_kmh: float = Field(default=0, ge=0, le=300)
    wind_direction_degrees: float = Field(default=0, ge=0, lt=360)
    spread_threshold: float = Field(default=0.5, ge=0, le=1)

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
    contract_version: Literal[CONTRACT_VERSION] = CONTRACT_VERSION
    engine: str
    deterministic: bool = True
    grid_key: str
    frames: list[SpreadFrame]
    metrics: dict[str, int | float]
