# Airline pixel logos

Four pixel-style airline mark adaptations are included: United (`UAL`), Delta (`DAL`), American (`AAL`), and Southwest (`SWA`). Each production PNG is exactly **28 × 28 pixels**, with a black background and three or four colors including black. Studio converts the pixels to RGB565 for USB transfer; the ESP32 renders the saved bitmap directly, without a PNG decoder or flight-data server.

The default **Default · 20px logo** preset scales the source bitmap to **20 × 20** at `(2, 2)` on the 64 × 32 panel. Flight number, airports, and aircraft type occupy the right column; time remaining and a green/white progress bar with a plane divider sit below. **Logo on right · 20px** moves the logo to `(42, 2)` and the top text to the left. **Large logo · 28px** retains the earlier 28 × 28 arrangement with altitude, speed, and distance.

Use the PNGs in `dist/logos/` directly in the editor, or download `dist/airline-pixel-logos.zip`. The pack includes `flight-details-layout.json`, a complete importable configuration with the new 20 × 20 layout and default flight filters. To keep existing filters, upload an individual PNG or use **Use bundled pixel logo** for the preview airline instead of importing that complete configuration. The older `large-logo-layout.json` remains included for the 28 × 28 arrangement.

The editor upgrades earlier local drafts once. Untouched factory layouts receive the new 20 × 20 layout; custom positions and uploaded images take precedence, with the new journey layers initially hidden. Choices to use initials persist. Saved board layouts and browser drafts retain their existing logos. To apply the corrected United blue, select a United sample flight, click **Use bundled pixel logo**, and **Save to ESP32**. Reflashing alone does not replace a previously saved layout.

## Asset preparation

The built-in image-generation tool generated one 1254 × 1254 source per airline. The exact final prompts are in `assets/logo-masters/prompts.json`; the original selected rasters are alongside that file. They are pixel-art adaptations for the flight display, not official downloadable brand assets.

For the hardware export, each source was composited onto black, sampled to 28 × 28 with nearest-neighbor interpolation, and mapped to its requested palette using nearest RGB color. The export keeps the whole source image and does not redraw the emblem. The resulting PNG pixel values are also embedded in `dist/defaults.json`. This ensures no network image request or external logo service is needed for rendering.

| Airline | Production image | Palette besides black |
| --- | --- | --- |
| United | `dist/logos/UAL.png` | `#0000FF`, `#FFFFFF` |
| Delta | `dist/logos/DAL.png` | `#E51937`, `#A6192E` |
| American | `dist/logos/AAL.png` | `#D71920`, `#0078D2`, `#FFFFFF` |
| Southwest | `dist/logos/SWA.png` | `#304CB2`, `#FFBF27`, `#E31837` |

United's original `#005DAA` blue included green (`RGB 0, 93, 170`). The hardware palette now uses pure blue (`RGB 0, 0, 255`, RGB565 `0x001F`) to remove that green component. The emblem geometry, white pixels, original source masters and archived offline sketch are preserved.

Configuration schema version 1 still accepts original 12 × 12 uploads. Bitmap dimensions may now range from 1 to 32 pixels per side; the pixel count must equal width times height. The existing 50-airline limit remains, with a total budget of 8,192 bitmap pixels to keep compact API payloads within the service's 128 KiB limit. Formatted layout imports may be up to 512 KiB; they are validated and serialized compactly for API requests.

The downloadable display examples are rendered from the same configuration, sample flights, and font as the browser and Python service. Physical panel color and brightness still depend on the LED hardware and brightness setting.
