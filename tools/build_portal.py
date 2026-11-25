"""Packt src/portal/index.html gezippt in src/portal_page.h (spart Flash, Browser entpacken selbst)."""
import gzip
from pathlib import Path
base = Path(__file__).resolve().parent.parent
html = (base / "src/portal/index.html").read_bytes()
gz = gzip.compress(html, 9, mtime=0)
lines = ["// Automatisch erzeugt von tools/build_portal.py aus src/portal/index.html. Nicht von Hand aendern.",
         "#pragma once", "#include <stdint.h>", f"static const uint32_t PORTAL_HTML_GZ_LEN = {len(gz)};",
         "static const uint8_t PORTAL_HTML_GZ[] = {"]
for i in range(0, len(gz), 32):
    lines.append("  " + ",".join(str(b) for b in gz[i:i + 32]) + ",")
lines.append("};")
(base / "src/portal_page.h").write_text("\n".join(lines) + "\n")
print(f"{len(html)} Byte HTML, gezippt {len(gz)} Byte")
