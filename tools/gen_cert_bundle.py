"""Baut data/x509_crt_bundle.bin im Format des ESP-IDF-Zertifikatsbuendels
(je Zertifikat: Laenge Subject, Laenge Public Key, beide 2 Byte big-endian,
dann Subject-DER und SubjectPublicKeyInfo-DER; vorn 2 Byte Anzahl; nach Subject sortiert).
Quelle: Stammzertifikate von macOS (tools/macos-roots.pem)."""
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization

base = Path(__file__).resolve().parent
pem = (base / "macos-roots.pem").read_bytes()
certs = x509.load_pem_x509_certificates(pem)
entries = {}
for c in certs:
    name = c.subject.public_bytes()
    key = c.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    entries[name] = key
data = bytearray(len(entries).to_bytes(2, "big"))
for name in sorted(entries):
    key = entries[name]
    data += len(name).to_bytes(2, "big") + len(key).to_bytes(2, "big") + name + key
(base.parent / "data/x509_crt_bundle.bin").write_bytes(data)
print(len(entries), "Zertifikate,", len(data), "Byte")
