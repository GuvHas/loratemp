# PlatformIO pre-build script: injects the current git short commit hash as
# the NODE_FW_VERSION preprocessor macro, so this node's cold-boot "sw" field
# (see formatPayload() in src/payload.cpp) always reflects the exact firmware
# build running instead of a hand-maintained version string someone has to
# remember to bump. Mirrors loragateway's own
# scripts/inject_git_version.py (same approach, same gateway repo, applied
# here to the sensor-node firmware).
#
# Falls back to "dev" if git isn't available or this isn't a git checkout
# (e.g. a source archive with no .git directory) -- payload.cpp also defines
# this fallback itself via #ifndef, so a build only misses the real hash if
# this script doesn't run at all, never a hard build failure.

import subprocess

Import("env")


def get_git_short_hash():
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=5,
        )
        if result.returncode != 0:
            return "dev"
        git_hash = result.stdout.decode("utf-8").strip()
        return git_hash if git_hash else "dev"
    except Exception:
        return "dev"


git_version = get_git_short_hash()

# CPPDEFINES value needs the escaped quotes so the compiler sees a proper C
# string literal (-DNODE_FW_VERSION=\"abc1234\"), not a bareword that would
# fail to compile as `NODE_FW_VERSION` used directly as a %s argument.
env.Append(CPPDEFINES=[("NODE_FW_VERSION", '\\"%s\\"' % git_version)])

print("inject_git_version.py: NODE_FW_VERSION = %s" % git_version)
