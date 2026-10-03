"""Build the Arduino-ESP32 compact trust bundle from certifi's public roots.

Format: Espressif esp_crt_bundle (ESP-IDF 4.4 / Arduino-ESP32 2.0.17).
Requires certifi and cryptography. Never use a workstation/proxy trust store.
"""
from pathlib import Path
import re
import struct
import certifi
from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

root = Path(__file__).resolve().parents[1]
certs = []
for pem in re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", Path(certifi.where()).read_bytes(), re.S):
    cert = x509.load_pem_x509_certificate(pem)
    subject = cert.subject.public_bytes()
    key = cert.public_key().public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo)
    certs.append((subject, key))
certs.sort(key=lambda item: item[0])
target = root / "firmware/certs/x509_crt_bundle"
target.parent.mkdir(exist_ok=True)
target.write_bytes(struct.pack(">H", len(certs)) + b"".join(struct.pack(">HH", len(s), len(k)) + s + k for s, k in certs))
print(f"Bundled {len(certs)} public roots from certifi {certifi.__version__}")
