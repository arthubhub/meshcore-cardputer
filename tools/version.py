#!/usr/bin/env python3
"""Firmware version of this project, computed from git (RELEASE-7).

    python3 tools/version.py              # prints the version, e.g. v0.8.0 or v0.8.0-3-g97fb867-dirty
    python3 tools/version.py --meshcore   # prints the MeshCore submodule tag, e.g. companion-v1.18.0
    python3 tools/version.py --self-test  # checks the rules below (run in CI)

Rules:
  - the nearest release tag, vX.Y.Z (a legacy "cardputer-" prefix is dropped): v0.8.0 on the tag itself,
    v0.8.0-3-g97fb867 three commits later, "-dirty" when files are modified;
  - no tag in reach (shallow clone): the short commit hash; no git at all: "unknown";
  - the CPADV_VERSION environment variable overrides everything (builds made outside git).
Used by tools/pio_version.py (every firmware build) and tools/make_dual.py. Standard library only.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TAG_PATTERNS = ("v[0-9]*", "cardputer-v[0-9]*")
LEGACY_PREFIX = "cardputer-"


def _git(args, cwd):
    try:
        out = subprocess.check_output(["git", "-C", cwd] + args, stderr=subprocess.DEVNULL, text=True)
        return out.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def normalize(described):
    """git describe output -> version string."""
    if not described:
        return "unknown"
    if described.startswith(LEGACY_PREFIX):
        described = described[len(LEGACY_PREFIX):]
    return described


def describe(root=ROOT, env=None, git=_git):
    env = os.environ if env is None else env
    forced = env.get("CPADV_VERSION", "").strip()
    if forced:
        return forced
    args = ["describe", "--tags", "--dirty", "--always"]
    for p in TAG_PATTERNS:
        args += ["--match", p]
    return normalize(git(args, root))


def meshcore_version(root=ROOT, git=_git):
    return git(["describe", "--tags", "--always"], os.path.join(root, "meshcore")) or "unknown"


def self_test():
    def fake(answer):
        return lambda args, cwd: answer
    cases = [
        (describe(env={}, git=fake("v0.8.0")), "v0.8.0"),
        (describe(env={}, git=fake("cardputer-v0.7.0")), "v0.7.0"),
        (describe(env={}, git=fake("cardputer-v0.7.0-2-g97fb867-dirty")), "v0.7.0-2-g97fb867-dirty"),
        (describe(env={}, git=fake("97fb867")), "97fb867"),            # shallow clone, no tag
        (describe(env={}, git=fake(None)), "unknown"),                 # no git
        (describe(env={"CPADV_VERSION": "v9.9.9"}, git=fake("v0.8.0")), "v9.9.9"),
        (describe(env={"CPADV_VERSION": "  "}, git=fake("v0.8.0")), "v0.8.0"),
        (meshcore_version(git=fake("companion-v1.18.0")), "companion-v1.18.0"),
        (meshcore_version(git=fake(None)), "unknown"),
    ]
    bad = [(got, want) for got, want in cases if got != want]
    for got, want in bad:
        print(f"FAIL: got {got!r}, want {want!r}", file=sys.stderr)
    print(f"version self-test: {len(cases) - len(bad)}/{len(cases)} ok")
    return not bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--meshcore", action="store_true", help="print the MeshCore submodule version")
    ap.add_argument("--self-test", action="store_true", help="check the version rules")
    args = ap.parse_args()
    if args.self_test:
        sys.exit(0 if self_test() else 1)
    print(meshcore_version() if args.meshcore else describe())


if __name__ == "__main__":
    main()
