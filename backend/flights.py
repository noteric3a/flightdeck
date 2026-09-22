from __future__ import annotations

import math
import time

from .models import Filters, Flight

# Small editable lookup. Callsign prefix is a best-effort operator hint, not route data.
AIRLINES = {
    "UAL": "UNITED",
    "DAL": "DELTA",
    "AAL": "AMERICAN",
    "SWA": "SOUTHWEST",
    "JBU": "JETBLUE",
    "ASA": "ALASKA",
    "FFT": "FRONTIER",
    "NKS": "SPIRIT",
    "BAW": "BRITISH",
    "JAL": "JAPAN",
    "ANA": "ANA",
    "ACA": "AIR CANADA",
    "FDX": "FEDEX",
    "UPS": "UPS",
    "DLH": "LUFTHANSA",
}


def haversine(lat1, lon1, lat2, lon2):
    r = math.pi / 180
    a = (
        math.sin((lat2 - lat1) * r / 2) ** 2
        + math.cos(lat1 * r) * math.cos(lat2 * r) * math.sin((lon2 - lon1) * r / 2) ** 2
    )
    a = min(1, max(0, a))
    return 6371 * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def bounding_boxes(f: Filters):
    angular = f.radius_km / 6371
    lat_delta = math.degrees(angular)
    lo, hi = max(-90, f.latitude - lat_delta), min(90, f.latitude + lat_delta)
    if lo == -90 or hi == 90:
        return [(lo, -180, hi, 180)]
    lon_delta = math.degrees(
        math.asin(min(1, math.sin(angular) / math.cos(math.radians(f.latitude))))
    )
    west, east = f.longitude - lon_delta, f.longitude + lon_delta
    if west < -180:
        return [(lo, west + 360, hi, 180), (lo, -180, hi, east)]
    if east > 180:
        return [(lo, west, hi, 180), (lo, -180, hi, east - 360)]
    return [(lo, west, hi, east)]


def filter_flights(flights: list[Flight], f: Filters, now=None) -> list[Flight]:
    now = time.time() if now is None else now
    result = []
    for flight in flights:
        if not f.include_ground and flight.on_ground:
            continue
        if (
            now - flight.position_time > f.max_position_age_s
            or flight.position_time > now + 60
        ):
            continue
        if f.airlines and flight.airline_code not in f.airlines:
            continue
        if f.callsign and f.callsign not in flight.callsign:
            continue
        if flight.altitude_ft is None:
            if f.min_altitude_ft > 0 or f.max_altitude_ft < 60000:
                continue
        elif not f.min_altitude_ft <= flight.altitude_ft <= f.max_altitude_ft:
            continue
        if flight.speed_knots is None:
            if f.min_speed_knots > 0:
                continue
        elif flight.speed_knots < f.min_speed_knots:
            continue
        distance = haversine(f.latitude, f.longitude, flight.latitude, flight.longitude)
        if distance <= f.radius_km:
            result.append(flight.model_copy(update={"distance_km": distance}))
    result.sort(
        key=lambda x: (
            -(x.altitude_ft if x.altitude_ft is not None else -1)
            if f.sort_by == "altitude"
            else x.distance_km,
            x.icao24,
        )
    )
    return result


def sample_flights(f: Filters, epoch=None):
    now = time.time()
    epoch = now if epoch is None else epoch
    # Illustrative trips, not the real schedules for these callsigns.
    journeys = [
        ("ORD", "HND", "B787-9", 510, 225),
        ("ATL", "JFK", "E175", 75, 45),
        ("MDW", "DEN", "B737-800", 35, 100),
        ("DFW", "LAX", "A321", 130, 25),
        (None, None, "C172", None, None),
        ("ORD", "SFO", "B737-800", None, None),
    ]
    specs = [
        ("a00001", "UAL247", "UAL", "UNITED", 0.08, 0.07, 35000, 452, 78, 0),
        ("a00002", "DAL1082", "DAL", "DELTA", -0.16, 0.13, 28000, 421, 142, -640),
        ("a00003", "SWA516", "SWA", "SOUTHWEST", 0.25, -0.22, 22000, 395, 312, 800),
        ("a00004", "AAL903", "AAL", "AMERICAN", -0.41, -0.28, 32000, 448, 265, 0),
        ("a00005", "N625EC", "", "PRIVATE", 0.02, 0.04, 2100, 108, 185, 200),
        ("a00006", "UAL700", "UAL", "UNITED", 0.01, -0.015, 600, 0, 0, 0),
    ]
    return [
        Flight(
            icao24=s[0],
            callsign=s[1],
            airline_code=s[2],
            airline_name=s[3],
            latitude=max(-90, min(90, f.latitude + s[4])),
            longitude=(f.longitude + s[5] + 180) % 360 - 180,
            altitude_ft=s[6],
            speed_knots=s[7],
            heading=s[8],
            vertical_rate_fpm=s[9],
            on_ground=i == 5,
            position_time=now,
            last_contact=now,
            origin_country="United States",
            departure_airport=journeys[i][0],
            arrival_airport=journeys[i][1],
            aircraft_type=journeys[i][2],
            departure_time=epoch - journeys[i][3] * 60
            if journeys[i][3] is not None
            else None,
            arrival_time=epoch + journeys[i][4] * 60
            if journeys[i][4] is not None
            else None,
            details_source="sample",
        )
        for i, s in enumerate(specs)
    ]
