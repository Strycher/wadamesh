#!/usr/bin/env python
"""
Inject our (Offband) patch-version marker into the wadamesh build. (#277)

Sets FIRMWARE_VERSION = "<nearest beta_* tag>+ob.<N>[+d]" where
  N     = git rev-list --count <tag>..HEAD on this branch (auto-bumps per commit)
  +d    = appended when the working tree has uncommitted changes (not a clean build)

so the ON-DEVICE reported version (companion API + touch "about"/boot screens)
unambiguously identifies OUR patched build on top of a specific wadamesh beta,
and moves with every commit. This overrides MyMesh.h's `#ifndef FIRMWARE_VERSION`
default ("v1.16.0-touch") because this header is force-included (-include) ahead
of MyMesh.h in every translation unit.

Also defines OFFBAND_PATCH_SHA (short SHA, +d) and OFFBAND_PATCH_BUILD_DATE for the
touch about screen (SHA surfaced THERE, not in FIRMWARE_VERSION, to stay under the
20-char companion-API cap at MyMesh.cpp:2670).

Emitted via a generated force-included HEADER, not command-line -D: the '+' and any
space are shell-word-split on Linux CI when passed as -D, breaking the compile.
Same portability pattern as scripts/inject_wifi_env.py and Offband's
scripts/inject_offband_version.py.
"""
import datetime
import os
import subprocess
import sys

Import("env")  # type: ignore[name-defined]  # noqa: F821


def _die(msg):
    # rule #9 (no silent failures): a version we cannot determine must FAIL the build,
    # never silently emit a lying default that gets flashed and misidentifies the device.
    sys.stderr.write(f"[offband-patch] FATAL: {msg}\n")
    raise SystemExit(1)


def _git(*args):
    """Run git; FAIL the build on any error instead of falling back to a wrong version."""
    try:
        r = subprocess.run(["git", *args], capture_output=True, text=True, check=False)
    except (subprocess.SubprocessError, FileNotFoundError) as e:
        _die(f"git {' '.join(args)} could not run ({e}); cannot determine build version.")
    if r.returncode != 0:
        _die(f"git {' '.join(args)} failed (rc={r.returncode}: {r.stderr.strip()}); cannot determine version.")
    return r.stdout.strip()


# A shallow clone makes `rev-list --count` wrong -> a lying version. Refuse to build.
if _git("rev-parse", "--is-shallow-repository") == "true":
    _die("shallow clone: commit count would be wrong. Run: git fetch --unshallow --tags")

# Nearest beta_* tag reachable from HEAD = the wadamesh base we branched off.
base_tag = _git("describe", "--tags", "--abbrev=0", "--match", "beta_*")
if not base_tag.startswith("beta_"):
    _die(f"no reachable beta_* tag (got '{base_tag}'); fetch tags: git fetch --tags")
# Commits on this branch since that base tag (0 right after branching).
count = _git("rev-list", "--count", f"{base_tag}..HEAD")
sha = _git("rev-parse", "--short=7", "HEAD")
# Dirty = any uncommitted change (tracked mods OR untracked new source). .pio build
# artifacts are gitignored, so they don't trip this.
dirty = "+d" if _git("status", "--porcelain") else ""

fw_version = f"{base_tag}+ob.{count}{dirty}"
if len(fw_version) > 20:  # MyMesh.cpp:2670 strzcpy caps the companion-API version at 20 chars
    sys.stderr.write(f"[offband-patch] WARNING: FIRMWARE_VERSION '{fw_version}' exceeds 20 chars; "
                     f"it WILL be truncated over the companion API.\n")
patch_sha = f"{sha}{dirty}"
build_date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


def _cstr(s):  # minimal C string-literal escape
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


# This script is registered UNPREFIXED (post-phase) so that `projenv` -- the
# environment PlatformIO uses to compile the PROJECT sources (src/ + variants/) --
# is importable and its CPPDEFINES are already populated from build_flags. At the
# `pre:` phase, build_flags have NOT yet been parsed into CPPDEFINES (it is empty),
# so the define-move below could not see them. We modify `projenv` directly because
# post-phase appends to the base `env` no longer propagate into the already-cloned
# projenv. Fall back to `env` only if projenv is somehow unavailable.
try:
    Import("projenv")  # type: ignore[name-defined]  # noqa: F821
    _benv = projenv  # type: ignore[name-defined]  # noqa: F821
except Exception:  # noqa: BLE001  (projenv not exported -> use base env)
    _benv = env  # type: ignore[name-defined]  # noqa: F821

# ---------------------------------------------------------------------------
# Windows CreateProcess 32767-char relief (MEASURED: the heltec_v4 TFT-touch
# cc1plus command line is 33572 chars -- 805 OVER the limit -> "CreateProcess:
# No such file or directory"). force_tempfile.py routes g++'s OWN invocation
# through an @response-file, but g++ then spawns cc1plus with every arg expanded
# and SCons cannot shorten THAT subprocess. So we move a block of simple, stable
# feature/pin -D defines OFF the projenv command line and into the force-included
# header below. Because that header is -include'd at the TOP of every project
# translation unit (before any #include), `#define FOO bar` is byte-for-byte
# equivalent to `-D FOO=bar` -- no macro-precedence change. This is safe even for
# library-facing defines (TFT_*/LV_*/RADIOLIB_*): LIBRARIES compile with their own
# env clones which we do NOT touch, so they keep their own copies; only projenv's
# command shrinks, and projenv's own sources get every moved define back via the
# header. We take VALUES verbatim (never rewritten) and assert moved+kept == original
# so no define can be silently dropped (rule #9). No-op off Windows (~2 MB limit).
_moved_defs = []
if sys.platform == "win32":
    _MOVE_PREFIXES = ("RADIOLIB_EXCLUDE_", "ENV_INCLUDE_", "P_LORA_", "PIN_",
                      "SX126X_", "TFT_", "ST7789_", "LV_", "DISPLAY_")
    _orig = list(_benv.get("CPPDEFINES", []))
    _kept = []
    for _d in _orig:
        if isinstance(_d, (list, tuple)):
            _name = str(_d[0])
            _val = _d[1] if len(_d) > 1 else None
        else:
            _name = str(_d)
            _val = None
        _vs = "" if _val is None else str(_val)
        # only move plain scalars: reject anything whose value carries quoting,
        # spaces or parens that could shift meaning between -D and #define.
        if _name.startswith(_MOVE_PREFIXES) and not any(c in _vs for c in ('"', "'", " ", "(", ")")):
            _moved_defs.append((_name, _val))
        else:
            _kept.append(_d)
    if len(_moved_defs) + len(_kept) != len(_orig):
        _die("CPPDEFINES partition lost entries; refusing to build a mis-defined binary.")
    _benv.Replace(CPPDEFINES=_kept)

# Write to scripts/build/ and force-include by BASENAME, NOT absolute path (a
# ~90-char absolute $BUILD_DIR path on every compile line is itself part of the
# overflow). Mirror inject_wifi_env.py: same out dir, on CPPPATH, bare -include.
# scripts/build/ is gitignored (generated).
_out_dir = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "build")  # type: ignore[name-defined]  # noqa: F821
os.makedirs(_out_dir, exist_ok=True)
_hdr = os.path.join(_out_dir, "offband_patch_version.h")
with open(_hdr, "w", encoding="utf-8") as _f:
    _f.write("#pragma once\n")
    _f.write(f"#define FIRMWARE_VERSION {_cstr(fw_version)}\n")        # overrides MyMesh.h:35 #ifndef
    _f.write(f"#define OFFBAND_PATCH_SHA {_cstr(patch_sha)}\n")
    _f.write(f"#define OFFBAND_PATCH_BUILD_DATE {_cstr(build_date)}\n")
    # Defines relocated off the command line (Windows CreateProcess relief).
    for _n, _v in _moved_defs:
        _f.write(f"#define {_n}\n" if _v is None else f"#define {_n} {_v}\n")
_benv.Append(CPPPATH=[_out_dir])
_benv.Append(CCFLAGS=["-include", "offband_patch_version.h"])

# Approx chars removed from each compile line: "-D <name>=<val> " per moved define.
_saved = sum(4 + len(n) + (1 + len(str(v)) if v is not None else 0) for n, v in _moved_defs)
print(f"[offband-patch] FIRMWARE_VERSION  = {fw_version}")
print(f"[offband-patch] OFFBAND_PATCH_SHA = {patch_sha}   date = {build_date}")
print(f"[offband-patch] moved {len(_moved_defs)} scalar defines off projenv cmdline "
      f"(~{_saved} chars/line) into force-header for Win32 CreateProcess relief")
