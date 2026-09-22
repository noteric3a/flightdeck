export const progressColors = { completed: "#00ff00", remaining: "#ffffff" };

// Right-facing plane icon in the same pixel grid as the display font.
export const plane = [
  "0010000",
  "0001000",
  "1001100",
  "1111111",
  "1001100",
  "0001000",
  "0010000",
];

export function journeyState(flight, now = Date.now() / 1000) {
  const departure = flight?.departure_time;
  const arrival = flight?.arrival_time;
  const validArrival = Number.isFinite(arrival) && arrival > 0;
  const validTrip = validArrival && Number.isFinite(departure) && departure > 0 && arrival > departure;
  const supplied = flight?.progress_percent;
  return {
    minutes: validArrival ? Math.max(0, Math.ceil((arrival - now) / 60)) : null,
    progress: validTrip
      ? Math.max(0, Math.min(1, (now - departure) / (arrival - departure)))
      : Number.isFinite(supplied) ? Math.max(0, Math.min(1, supplied / 100)) : null,
  };
}

export function remainingText(flight, now = Date.now() / 1000) {
  const { minutes } = journeyState(flight, now);
  if (minutes === null) return "--";
  if (minutes >= 6000) return ">99H";
  return minutes >= 60
    ? `${Math.floor(minutes / 60)}H${String(minutes % 60).padStart(2, "0")}M`
    : `${minutes}M`;
}
