// One location fix, only after the user clicks the location button. No watcher
// or IP-location service is used; the saved coordinates become a fixed center.
export function currentLocation({
  geolocation = globalThis.navigator?.geolocation,
  secureContext = globalThis.isSecureContext,
  timeoutMs = 12000,
} = {}) {
  if (!secureContext)
    return Promise.reject(new Error("Location needs HTTPS or localhost. Enter coordinates manually."));
  if (!geolocation?.getCurrentPosition)
    return Promise.reject(new Error("This browser cannot provide location. Enter coordinates manually."));
  return new Promise((resolve, reject) => {
    let finished = false;
    const finish = (error, location) => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      if (error) reject(new Error(error));
      else resolve(location);
    };
    // The browser's timeout can exclude time spent waiting for permission.
    // This timer also restores the UI if the prompt is left unanswered.
    const timeoutMessage = "Location request timed out. Try again or enter coordinates manually.";
    const timer = setTimeout(() => finish(timeoutMessage), timeoutMs);
    try {
      geolocation.getCurrentPosition(position => {
        const { latitude, longitude, accuracy } = position?.coords || {};
        if (!Number.isFinite(latitude) || !Number.isFinite(longitude) ||
            latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
          finish("The browser returned an invalid location. Enter coordinates manually.");
          return;
        }
        finish(null, {
          latitude: Number(latitude.toFixed(6)),
          longitude: Number(longitude.toFixed(6)),
          accuracy: Number.isFinite(accuracy) && accuracy >= 0 ? accuracy : null,
        });
      }, error => {
        finish(error?.code === 1
          ? "Location permission denied. Allow location in your browser or enter coordinates manually."
          : error?.code === 2
            ? "Location is unavailable. Try again or enter coordinates manually."
            : error?.code === 3
              ? timeoutMessage
              : "Could not get this computer's location. Enter coordinates manually.");
      }, { enableHighAccuracy: false, maximumAge: 0, timeout: timeoutMs });
    } catch {
      finish("Could not get this computer's location. Enter coordinates manually.");
    }
  });
}
