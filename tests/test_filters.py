import time

import pytest

from backend.flights import bounding_boxes, filter_flights, haversine, sample_flights
from backend.models import defaults


def test_radius_and_operator_filters():
    f = defaults().filters
    flights = sample_flights(f)
    matches = filter_flights(flights, f)
    assert len(matches) == 5
    assert [x.distance_km for x in matches] == sorted(x.distance_km for x in matches)
    assert [
        x.callsign
        for x in filter_flights(flights, f.model_copy(update={"airlines": ["DAL"]}))
    ] == ["DAL1082"]
    assert not filter_flights(flights, f.model_copy(update={"radius_km": 1}))
    assert (
        len(filter_flights(flights, f.model_copy(update={"include_ground": True}))) == 6
    )


def test_altitude_speed_and_stale_filters():
    f = defaults().filters
    flights = sample_flights(f)
    filtered = filter_flights(
        flights, f.model_copy(update={"min_altitude_ft": 30000, "min_speed_knots": 440})
    )
    assert {x.callsign for x in filtered} == {"UAL247", "AAL903"}
    assert not filter_flights(
        [flights[0].model_copy(update={"position_time": time.time() - 301})], f
    )
    assert not filter_flights(
        [flights[0].model_copy(update={"position_time": time.time() + 90})], f
    )
    unknown = flights[0].model_copy(update={"altitude_ft": None, "speed_knots": None})
    assert filter_flights([unknown], f)
    assert not filter_flights([unknown], f.model_copy(update={"min_speed_knots": 1}))


def test_haversine_antimeridian_and_poles():
    assert haversine(0, 179.9, 0, -179.9) == pytest.approx(22.24, rel=0.01)
    assert haversine(0, 0, 0, 0) == 0
    assert haversine(0, 0, 0, 180) == pytest.approx(20015, rel=0.001)
    f = defaults().filters.model_copy(
        update={"latitude": 0, "longitude": 179.9, "radius_km": 100}
    )
    boxes = bounding_boxes(f)
    assert len(boxes) == 2
    for south, west, north, east in boxes:
        assert -180 <= west < east <= 180 and south < north
    polar = bounding_boxes(f.model_copy(update={"latitude": 89.9}))
    assert len(polar) == 1 and polar[0][1] == -180 and polar[0][3] == 180
