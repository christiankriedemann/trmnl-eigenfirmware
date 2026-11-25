"""Einstellungen per USB an die Eigenfirmware schicken.

Das Geraet nimmt nach einem Neustart 2,5 s lang Befehle an (danach 60 s nach jedem Befehl).
Werte aus Umgebungsvariablen: "set td_token @TODOIST_API_TOKEN" liest $TODOIST_API_TOKEN,
so steht das Geheimnis weder in der Shell-Historie noch in der Ausgabe.

  uv run --with pyserial python tools/geraet.py [--reset] "status" "set td_token @TODOIST_API_TOKEN" run
"""
import base64
import glob
import os
import sys
import time

import serial


def port():
    p = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not p:
        sys.exit("kein Geraet an USB")
    return p[0]


def main():
    args = sys.argv[1:]
    reset = "--reset" in args
    args = [a for a in args if a != "--reset"]
    s = serial.Serial(port(), 115200, timeout=0.2)
    if reset:
        # USB-JTAG: RTS kurz ziehen = Neustart
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        s.close()
        time.sleep(0.8)
        for _ in range(50):
            try:
                s = serial.Serial(port(), 115200, timeout=0.2)
                break
            except Exception:
                time.sleep(0.1)
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 8:
        buf += s.read(256)
        if b"bereit" in buf:
            break
    if b"bereit" not in buf:
        print(buf.decode(errors="replace"))
        sys.exit("Geraet meldet sich nicht (Befehlsfenster verpasst?)")
    print(buf.decode(errors="replace").strip().splitlines()[-1])
    for cmd in args:
        parts = cmd.split(" ", 2)
        if len(parts) == 3 and parts[2].startswith("@"):
            val = os.environ.get(parts[2][1:], "")
            if not val:
                sys.exit(f"Umgebungsvariable {parts[2][1:]} fehlt")
            if "\n" in val:
                cmd = f"setb64 {parts[1]} " + base64.b64encode(val.encode()).decode()
            else:
                cmd = f"{parts[0]} {parts[1]} {val}"
        data = (cmd + "\n").encode()
        for i in range(0, len(data), 64):  # kleiner Empfangspuffer im Geraet
            s.write(data[i:i + 64])
            s.flush()
            time.sleep(0.03)
        out = b""
        t1 = time.time()
        while time.time() - t1 < 4:
            chunk = s.read(512)
            out += chunk
            text = out.decode(errors="replace")
            if cmd.startswith("status") and "fertig" in text:
                break
            if not cmd.startswith("status") and text.strip() and chunk == b"":
                break
        shown = cmd.split(" ")[0] + (" " + cmd.split(" ")[1] if " " in cmd else "")
        print(f"> {shown}\n{out.decode(errors='replace').strip()}")
    # danach weiter mitlesen, bis Ruhe ist
    t2 = time.time()
    while time.time() - t2 < float(os.environ.get("MITLESEN", "0")):
        line = s.readline()
        if line:
            print(line.decode(errors="replace").rstrip())


if __name__ == "__main__":
    main()
