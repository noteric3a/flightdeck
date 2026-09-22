# Validation record — portfolio cleanup

Checked on **September 18, 2026** against the supplied source plus the documented packaging changes.

## Checks executed in this environment

| Check | Result |
| --- | --- |
| Python suite | **75 passed**: 53 supplied tests plus 22 new source-packaging checks |
| JavaScript suite | **13 passed** |
| Native frame decoder / renderer parity | Covered by the passing supplied Python suite |
| Packaging policy | Tests cover environment variants, credential paths, cache/log/database exclusions, retained editor assets/examples, symlinks, and archive self-exclusion |
| Browser screenshot / interaction | Not completed: Playwright's Chromium executable was unavailable |
| Docker image | Not built or run here |
| ESP32 / ESP32-S3 firmware | Not compiled or flashed during this cleanup |
| Physical matrix, Wi-Fi, wiring, scan rate, power | Not tested |
| Live OpenSky / FlightAware access | Not tested; no live-provider or paid API requests made |

Commands executed from the project root:

```bash
python -m pytest -q
node --test tests/editor.test.mjs
```

## Actual local environment

Python **3.13.5**, Node.js **22.16.0**. The installed packages were:

| Package | Installed version |
| --- | --- |
| fastapi | 0.128.2 |
| uvicorn | 0.48.0 |
| httpx | 0.28.1 |
| pydantic | 2.13.4 |
| python-dotenv | 1.2.2 |
| pytest | 9.0.2 |
| pytest-asyncio | 1.3.0 |

**These installed versions differ from the supplied pinned requirements.** Passing tests establish behavior in this environment, not successful installation or compatibility of every pinned dependency. The pinned dependency files and CI definitions were left unchanged. A clean install with those exact pins remains a separate validation step.

## Scope of changes

The service, renderer, browser editor, firmware, presets, logo assets, and deployment definitions are unchanged. Changes include a portfolio-oriented README, preservation of the original instructions in `USAGE.md`, contributor/security notes, editor/Git hygiene, a side-effect-free packaging function, and 22 packaging tests.

The packaging script excludes known sensitive/runtime paths; it is **not a general secret scanner**. Review the resulting archive before public release.

## Historical validation

The supplied archive's original validation record is preserved unchanged in [validation-original.md](validation-original.md). Its September 9, 2026 firmware-build and environment claims were not independently reverified during this cleanup and must not be substituted for the current results above.
