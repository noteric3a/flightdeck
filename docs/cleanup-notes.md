# Cleanup scope

This portfolio pass reorganizes project presentation without rewriting the application.

- The original README is preserved byte-for-byte as root `USAGE.md` so its relative paths still work.
- The new README explains the architecture, demo mode, implementation, project map, and validation boundaries.
- The existing rendered sample image is labeled as software output, not a physical-device photograph.
- `CONTRIBUTING.md`, `SECURITY.md`, `.editorconfig`, and `.gitattributes` document development and repository conventions.
- `.gitignore` adds environment variants, OS artifacts, logs, SQLite sidecars, and generated source ZIPs without ignoring the working `dist/` editor.
- `scripts/package_source.py` now has testable functions and an explicit main guard, skips known local state and symlinks, and preserves source/example resources.
- `tests/test_source_packaging.py` adds 22 checks. The full suite has 75 passing Python tests in the recorded local environment.

No service/firmware behavior, API credentials, dependency pins, hardware claims, license grants, or research metrics were invented or changed. See `validation.md` for exact tests and limitations.
