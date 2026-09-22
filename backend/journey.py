"""UTC-based journey estimates shared by the editor and device renderer."""

import math
import time

PLANE = (
    "0010000",
    "0001000",
    "1001100",
    "1111111",
    "1001100",
    "0001000",
    "0010000",
)
COMPLETED = "#00ff00"
REMAINING = "#ffffff"


def journey_state(flight, now=None):
    now = time.time() if now is None else now
    departure = flight.departure_time if flight else None
    arrival = flight.arrival_time if flight else None
    supplied = flight.progress_percent if flight else None
    valid_arrival = arrival is not None and arrival > 0
    valid_trip = valid_arrival and departure is not None and 0 < departure < arrival
    minutes = max(0, math.ceil((arrival - now) / 60)) if valid_arrival else None
    progress = (
        (now - departure) / (arrival - departure)
        if valid_trip
        else supplied / 100
        if supplied is not None
        else None
    )
    return minutes, max(0, min(1, progress)) if progress is not None else None


def remaining_text(flight, now=None):
    minutes, _ = journey_state(flight, now)
    if minutes is None:
        return "--"
    if minutes >= 6000:
        return ">99H"
    return f"{minutes // 60}H{minutes % 60:02d}M" if minutes >= 60 else f"{minutes}M"
