# Contributing

Use a focused branch and explain what changed, why, and how it was tested. Keep USB protocol changes coordinated across JavaScript and C++. Legacy Python endpoints remain separately tested. Regenerate firmware assets after changing Studio defaults or font data with `node scripts/generate_device_assets.mjs`. Regenerate the PNG/layout downloads and logo ZIP with `python scripts/generate_logo_pack.py`; both generators support `--check`.

## Local checks

Install the documented development requirements, then run `python -m pytest -q` and `node --test tests/*.test.mjs`. Firmware changes also need the appropriate PlatformIO build and, before claiming hardware compatibility, a test on the actual board and panel.

Do not commit credentials, databases, generated caches, or local simulator frames. Keep required `dist/` editor assets in source control. `python scripts/package_source.py` creates the portable source archive while excluding known local/private file patterns; it is not a complete secret scanner.

## Reporting evidence

State the environment, exact command, result, and limitations. Separate simulated data from live flights and physical measurements from software tests. Do not invent latency, refresh-rate, or power figures. Use synthetic examples in tests so contributors do not need paid flight-provider access.

## Documentation and rights

Keep README.md focused on the project; put detailed operation in USAGE.md and docs/. Preserve attribution for libraries and supplied assets. Do not add a license or third-party images without confirming the relevant rights.
