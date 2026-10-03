# Public TLS trust bundle

`x509_crt_bundle` contains the public Mozilla trust anchors distributed by
**certifi 2026.07.22**, converted to Espressif's compact certificate bundle format
by `scripts/generate_cert_bundle.py`. Generated for this update on 2026-10-03.
It contains 121 public roots and no keys, user credentials or workstation roots.

The source certificate data is available from https://github.com/certifi/python-certifi
and https://pypi.org/project/certifi/2026.7.22/ under the included Mozilla Public
License 2.0 (`LICENSE.certifi`). The generated representation contains each
certificate's subject DER and public-key DER, sorted by subject, with the
length-prefixed format consumed by Arduino-ESP32's `esp_crt_bundle.c`.

To update public trust roots, install current `certifi` and `cryptography`, run
`python scripts/generate_cert_bundle.py`, and rebuild/reflash. Normal builds use
the committed bundle and do not contact a certificate-download service. The
firmware uses hostname/certificate validation; there is no insecure TLS fallback.
