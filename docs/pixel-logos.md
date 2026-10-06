# Airline pixel logos

Four pixel-style airline mark adaptations are included: United (`UAL`), Delta (`DAL`), American (`AAL`), and Southwest (`SWA`). Each production PNG is exactly **28 × 28 pixels**, with a black background and three or four colors including black. Studio converts the pixels to RGB565 for USB transfer; the ESP32 renders the saved bitmap directly, without a PNG decoder or flight-data server.

The default **Default · 20px logo** preset scales the source bitmap to **20 × 20** at `(2, 2)` on the 64 × 32 panel. Flight number, airports, and aircraft type occupy the right column; time remaining and a green/white progress bar with a plane divider sit below. **Logo on right · 20px** moves the logo to `(42, 2)` and the top text to the left. **Large logo · 28px** retains the earlier 28 × 28 arrangement with altitude, speed, and distance.

Use the PNGs in `dist/logos/` directly in the editor, or download `dist/airline-pixel-logos.zip`. The pack includes `flight-details-layout.json`, a complete importable configuration with the new 20 × 20 layout and default flight filters. To keep existing filters, upload an individual PNG or use **Use bundled pixel logo** for the preview airline instead of importing that complete configuration. The older `large-logo-layout.json` remains included for the 28 × 28 arrangement.

Studio compares older factory bitmaps pixel-for-pixel at RGB565 precision when loading browser drafts or saved board layouts. Recognized United, Delta and Southwest assets upgrade to the corrected versions; custom images, removed logos, positions and filters survive. The previous factory aircraft/plane accent colors upgrade to light blue (`#80BFFF`). Click **Save to ESP32** to persist the corrected draft. Reflashing alone does not replace a previously saved layout. To replace a custom image, select that airline and click **Use bundled pixel logo**, then save.

## Asset preparation

The built-in image-generation tool generated one 1254 × 1254 source per airline. The exact final prompts are in `assets/logo-masters/prompts.json`; the original selected rasters are alongside that file. They are pixel-art adaptations for the flight display, not official downloadable brand assets.

The initial sources were sampled to 28 × 28 and palette-mapped. The production pixel data in `dist/defaults.json` is now authoritative: it includes the corrected palettes and Southwest diagonal bands. `node scripts/generate_device_assets.mjs` produces the autonomous layout and offline-demo bitmaps/palettes. `python scripts/generate_logo_pack.py` exports the exact same pixels as PNGs, importable layouts, rendered examples and the ZIP download. Both generators have a `--check` mode. No network image request or external logo service is used at runtime.

| Airline | Production image | Palette besides black |
| --- | --- | --- |
| United | `dist/logos/UAL.png` | `#0000FF`, `#FFFFFF` |
| Delta | `dist/logos/DAL.png` | `#FF0000`, `#B00000` |
| American | `dist/logos/AAL.png` | `#D71920`, `#0078D2`, `#FFFFFF` |
| Southwest | `dist/logos/SWA.png` | `#0000FF`, `#FF0000`, `#FFBF00` |

United uses pure blue (`RGB 0, 0, 255`, RGB565 `0x001F`) in both firmware paths. Delta uses only the red channel for both shaded halves. Southwest's heart has blue at the lower left, a wide red band descending to the right, and yellow at the upper right, following the orientation shown in [Southwest's company-promise image](https://www.southwest.com/swa-resources/images/responsive/citizenship/about-southwest/About-Company-Promise-opt.JPG). These saturated hardware palettes are display adaptations, not color-managed reproductions of airline brand specifications. The original generated source masters remain unchanged.

Configuration schema version 1 still accepts original 12 × 12 uploads. Bitmap dimensions may now range from 1 to 32 pixels per side; the pixel count must equal width times height. The existing 50-airline limit remains, with a total budget of 8,192 bitmap pixels to keep compact API payloads within the service's 128 KiB limit. Formatted layout imports may be up to 512 KiB; they are validated and serialized compactly for API requests.

The downloadable display examples are rendered from the same configuration, sample flights, and font as the browser and Python service. Physical panel color and brightness still depend on the LED hardware and brightness setting.
