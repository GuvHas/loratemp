Import("env")

import importlib.util
import subprocess
import sys


def ensure_module(module_name: str) -> None:
    if importlib.util.find_spec(module_name) is not None:
        return

    print(f"[prebuild] Missing Python module '{module_name}', installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", module_name])


for module in ("intelhex",):
    ensure_module(module)
