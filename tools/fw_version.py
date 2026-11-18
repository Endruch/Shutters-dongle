Import("env")

import os
import subprocess


def tag():
    if os.environ.get("GITHUB_REF_TYPE") == "tag":
        return os.environ["GITHUB_REF_NAME"]
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"], cwd=env.subst("$PROJECT_DIR"), stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "v0.0-dev"


version = tag().removeprefix("v")
print("Firmware version: " + version)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
