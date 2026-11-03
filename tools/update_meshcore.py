#!/usr/bin/env python3
"""Moves the meshcore/ submodule to an official MeshCore release.

    python3 tools/update_meshcore.py              # latest companion-vX.Y.Z release
    python3 tools/update_meshcore.py companion-v1.18.0
    python3 tools/update_meshcore.py --dry-run    # only tell what would change

Only final releases are considered (companion-vX.Y.Z, no -rc / -beta). After the move:
  - meshcore_base.ini is regenerated (tools/sync_meshcore_base.py),
  - the MeshCore files our code depends on are compared between the two versions, so a reviewer
    knows where to look if something breaks.
Nothing is committed: build and test, then commit meshcore/ and meshcore_base.ini together.

In GitHub Actions the results go to $GITHUB_OUTPUT (changed, old, new, major_change) and to
the job summary.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUB = os.path.join(ROOT, "meshcore")
TAG_RE = re.compile(r"^companion-v(\d+)\.(\d+)\.(\d+)$")

# MeshCore files our firmware builds on. A change in one of them deserves a look.
WATCHED = [
    "src/helpers/BaseChatMesh.h", "src/helpers/BaseChatMesh.cpp",   # standalone mesh (CardputerMesh)
    "src/helpers/ESP32Board.h", "src/MeshCore.h", "src/Mesh.h",       # board base class
    "src/helpers/ui/DisplayDriver.h",                                # companion screen (CardputerDisplay)
    "src/helpers/radiolib/",                                         # SX1262 wrapper
    "src/helpers/sensors/",                                          # GPS of the companion
    "src/helpers/AutoDiscoverRTCClock.h", "src/helpers/IdentityStore.h",
    "examples/companion_radio/",                                     # the companion firmware (app1)
    "platformio.ini",                                                # shared build settings
]


def git(*args, check=True):
    r = subprocess.run(["git", "-C", SUB, *args], capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"git {' '.join(args)} failed:\n{r.stderr}")
    return r.stdout.strip()


def version(tag):
    m = TAG_RE.match(tag)
    return tuple(int(x) for x in m.groups()) if m else None


def current_tag():
    return git("describe", "--tags", "--exact-match", "--match", "companion-v*", check=False) or git("rev-parse", "--short", "HEAD")


def out(key, value):
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a") as f:
            f.write(f"{key}={value}\n")


def summary(text):
    print(text)
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a") as f:
            f.write(text + "\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    if not os.path.exists(os.path.join(SUB, ".git")):
        sys.exit("meshcore/ is empty: run  git submodule update --init")

    git("fetch", "--quiet", "--tags", "origin")
    tags = [t for t in git("tag", "--list", "companion-v*").split() if version(t)]
    if not tags:
        sys.exit("no companion-vX.Y.Z release found")
    target = args[0] if args else max(tags, key=version)
    if target not in tags:
        sys.exit(f"{target} is not an official release (known: {', '.join(sorted(tags, key=version)[-5:])})")

    old = current_tag()
    out("old", old)
    out("new", target)
    if old == target:
        summary(f"MeshCore is already on {target}: nothing to do.")
        out("changed", "false")
        return

    ov, nv = version(old), version(target)
    if ov and nv and nv < ov:
        sys.exit(f"{target} is older than the current {old}: refusing to go back (pass it explicitly to force)"
                 if not args else f"going back from {old} to {target} is not supported by this tool")
    major = bool(ov and nv and nv[0] != ov[0])
    out("major_change", "true" if major else "false")

    old_ref = git("rev-parse", "HEAD")
    changed = git("diff", "--name-only", old_ref, target, "--", *WATCHED).splitlines()
    n_commits = git("rev-list", "--count", f"{old_ref}..{target}")
    summary(f"## MeshCore {old} -> {target}")
    summary(f"{n_commits} commits upstream." + (" **Major version change: review before merging.**" if major else ""))
    if changed:
        summary("Files our firmware depends on that changed (look here first if something breaks):")
        for f in changed:
            summary(f"- `{f}`")
    else:
        summary("None of the MeshCore files our firmware depends on changed.")

    if dry:
        out("changed", "false")
        return
    git("checkout", "--quiet", target)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "sync_meshcore_base.py")])
    if r.returncode:
        sys.exit("could not regenerate meshcore_base.ini")
    out("changed", "true")
    summary(f"\nmeshcore/ is now on {target}. Next: build every env, run the simulator, flash and test,"
            " then commit meshcore and meshcore_base.ini.")


if __name__ == "__main__":
    main()
