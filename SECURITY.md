# Security notes

Current firmware stores the Wi-Fi password and FlightAware AeroAPI key in **unencrypted ESP32 NVS**, provisioned through a user-selected USB serial port. Do not distribute a flash dump containing credentials. Use a trusted desktop, trusted Studio origin and trusted physical USB connection. Flash encryption/secure boot are not configured by this project.

Studio keeps secret form values only while editing. It sends them directly to a firmware-verified board, clears them after saving or closing, and never saves them to localStorage/layout exports. Status exposes only `has_password` and `has_api_key` flags. USB command access is physical and has no additional authentication; any program able to open the port can reconfigure it. No network configuration/control server is exposed by the board.

Provider traffic uses a fixed FlightAware HTTPS origin, an embedded public-root certificate bundle, hostname checking, NTP time and disabled redirects. Never add `setInsecure()`. Keep the firmware trust bundle current. Live mode makes potentially billable API requests, with bounded pages and a persistent UTC-hour request cap; this is not a monetary spending cap.

Keep `.env`, local `.env.*`, `firmware/include/secrets.h`, tokens, real Wi-Fi details and personal coordinates out of public Git and shared ZIPs. The old backend/example secret files remain legacy references only. Use synthetic credentials/responses in tests. Never post keys or private credentials in an issue; report suspected vulnerabilities privately to the maintainer where possible.

If a key has been shared or committed, revoke/rotate it. Removing it from a current file does not remove historical copies. The source packaging exclusion list is not a general secret scanner. Review staged changes before publishing. The Studio launcher binds only to loopback and serves editor files only.
