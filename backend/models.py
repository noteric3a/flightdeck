from __future__ import annotations

import json
from pathlib import Path
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

ROOT = Path(__file__).resolve().parent.parent
Color = Annotated[str, Field(pattern=r"^#[0-9a-fA-F]{6}$")]


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", allow_inf_nan=False)


class Element(StrictModel):
    id: str = Field(pattern=r"^[a-z_]{1,24}$")
    field: Literal[
        "logo",
        "callsign",
        "airline",
        "altitude",
        "speed",
        "distance",
        "heading",
        "vertical_rate",
        "route",
        "aircraft",
        "eta",
        "progress",
    ]
    x: int = Field(ge=0, le=63)
    y: int = Field(ge=0, le=31)
    width: int = Field(ge=1, le=64)
    height: int = Field(ge=1, le=32)
    scale: int = Field(ge=1, le=3)
    visible: bool
    color: Color


class Layout(StrictModel):
    width: Literal[64] = 64
    height: Literal[32] = 32
    brightness: int = Field(ge=1, le=255)
    units: Literal["aviation", "metric"] = "aviation"
    background: Color = "#000000"
    elements: list[Element] = Field(min_length=1, max_length=16)

    @model_validator(mode="after")
    def validate_bounds(self):
        if len({e.id for e in self.elements}) != len(self.elements):
            raise ValueError("Layer IDs must be unique")
        for e in self.elements:
            if e.x + e.width > self.width or e.y + e.height > self.height:
                raise ValueError(f"Layer {e.id} extends outside the display")
            if e.field == "progress" and (e.width < 7 or e.height < 7):
                raise ValueError("The progress layer must be at least 7 × 7 pixels")
        return self


class Filters(StrictModel):
    latitude: float = Field(ge=-90, le=90)
    longitude: float = Field(ge=-180, le=180)
    radius_km: float = Field(ge=1, le=250)
    min_altitude_ft: float = Field(ge=0, le=60000)
    max_altitude_ft: float = Field(ge=0, le=60000)
    min_speed_knots: float = Field(ge=0, le=2000)
    airlines: list[Annotated[str, Field(pattern=r"^[A-Z]{3}$")]] = Field(max_length=50)
    callsign: str = Field(max_length=8, pattern=r"^[A-Z0-9 ]*$")
    include_ground: bool
    sort_by: Literal["distance", "altitude"]
    max_position_age_s: int = Field(ge=30, le=600)

    @model_validator(mode="after")
    def validate_altitude(self):
        if self.min_altitude_ft > self.max_altitude_ft:
            raise ValueError("Minimum altitude exceeds maximum altitude")
        return self


class Logo(StrictModel):
    # Keep the original defaults so existing 12-pixel API clients still work.
    width: int = Field(default=12, ge=1, le=32)
    height: int = Field(default=12, ge=1, le=32)
    pixels: list[Color | None] = Field(min_length=1, max_length=1024)

    @model_validator(mode="after")
    def validate_pixel_count(self):
        if len(self.pixels) != self.width * self.height:
            raise ValueError("Logo pixel count must match its width and height")
        return self


class Configuration(StrictModel):
    schema_version: Literal[1] = 1
    layout: Layout
    filters: Filters
    rotation_seconds: int = Field(ge=5, le=300)
    logos: dict[Annotated[str, Field(pattern=r"^[A-Z]{3}$")], Logo] = Field(
        max_length=50
    )

    @model_validator(mode="after")
    def validate_logo_budget(self):
        if sum(len(logo.pixels) for logo in self.logos.values()) > 8192:
            raise ValueError("Airline logos may contain at most 8192 pixels in total")
        return self


class Flight(StrictModel):
    icao24: str = Field(pattern=r"^[0-9a-f]{6}$")
    callsign: str = Field(max_length=8)
    airline_code: str
    airline_name: str
    latitude: float = Field(ge=-90, le=90)
    longitude: float = Field(ge=-180, le=180)
    altitude_ft: float | None
    speed_knots: float | None = Field(default=None, ge=0)
    heading: float | None = Field(default=None, ge=0, le=360)
    vertical_rate_fpm: float | None
    on_ground: bool
    position_time: float
    last_contact: float
    origin_country: str
    distance_km: float | None = None
    departure_airport: str | None = Field(default=None, pattern=r"^[A-Z0-9]{3,4}$")
    arrival_airport: str | None = Field(default=None, pattern=r"^[A-Z0-9]{3,4}$")
    aircraft_type: str | None = Field(default=None, max_length=24)
    departure_time: float | None = Field(default=None, ge=0)
    arrival_time: float | None = Field(default=None, ge=0)
    progress_percent: float | None = Field(default=None, ge=0, le=100)
    details_source: Literal["sample", "aeroapi"] | None = None


def defaults() -> Configuration:
    return Configuration.model_validate(
        json.loads((ROOT / "dist/defaults.json").read_text())
    )
