# Vor jedem Build: Webinterface neu packen
Import("env")
import subprocess, sys
subprocess.run([sys.executable, "tools/build_portal.py"], check=True)
