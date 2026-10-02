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


# A shallow clone makes `rev-list --count` wrong -> a lying version. Deepen it if we
# can, and refuse to build if we cannot — never emit a count from truncated history.
#
# This is not hypothetical: actions/checkout@v4 defaults to fetch-depth 1, so EVERY
# CI and release build arrives shallow. Fixing it here rather than in the workflows
# covers both ci.yml and release.yml, and avoids editing release.yml, which is
# upstream's file and would conflict on every future upstream take.
if _git("rev-parse", "--is-shallow-repository") == "true":
    print("[offband-patch] shallow clone detected — deepening (git fetch --unshallow --tags)")
    try:
        _r = subprocess.run(["git", "fetch", "--unshallow", "--tags"],
                            capture_output=True, text=True, check=False)
    except (subprocess.SubprocessError, FileNotFoundError) as _e:
        _die(f"shallow clone and `git fetch --unshallow` could not run ({_e}); "
             "commit count would be wrong.")
    if _r.returncode != 0 or _git("rev-parse", "--is-shallow-repository") == "true":
        _die("shallow clone and `git fetch --unshallow --tags` did not deepen it "
             f"(rc={_r.returncode}: {_r.stderr.strip()}); commit count would be wrong.")

# Nearest beta_* tag reachable from HEAD = the wadamesh base we branched off.
base_tag = _git("describe", "--tags", "--abbrev=0", "--match", "beta_*")
if not base_tag.startswith("beta_"):
    _die(f"no reachable beta_* tag (got '{base_tag}'); fetch tags: git fetch --tags")

# Cross-check describe against the newest beta_* tag that is actually an ancestor.
#
# WHY (#92): the shallow guard above only catches truncated HISTORY. CI hit a case
# where the history was complete but the TAG SET was not, and `describe` happily
# answered with an older tag — beta_44+ob.871 in CI against beta_85+ob.11 locally,
# from the same commit. Nothing failed; the build just stamped a version naming the
# wrong upstream base. That is the exact outcome `_die` exists to prevent, one level
# down from the case it was written for: not "we cannot determine the version" but
# "we determined it wrongly and said nothing".
#
# `describe` picks the nearest tag by graph distance. On a straight trunk that is
# also the newest, so the two agree; when they disagree, either the tag set is
# incomplete or the graph is stranger than we think, and both are worth stopping for.
_tags = [t for t in _git("tag", "--list", "beta_*").splitlines() if t.strip()]
# `merge-base --is-ancestor` exits 0 for yes and 1 for no. ANY other code is an
# error — a missing object, an unreadable repo — and must not be read as "no".
# Collapsing error into no is what let the first version of this guard pass
# silently in CI, which is the same mistake it was written to catch.
_ancestors, _undecidable = [], []
for _t in _tags:
    _rc = subprocess.run(["git", "merge-base", "--is-ancestor", _t, "HEAD"],
                         capture_output=True, text=True, check=False)
    if _rc.returncode == 0:
        _ancestors.append(_t)
    elif _rc.returncode != 1:
        _undecidable.append(f"{_t}(rc={_rc.returncode} {_rc.stderr.strip()[:60]})")
if _undecidable:
    _die("cannot decide tag ancestry, so the version base cannot be trusted: "
         f"{', '.join(_undecidable[:5])}"
         f"{', ...' if len(_undecidable) > 5 else ''}")

# Always say what we concluded. A guard whose success is silent cannot be
# debugged from a build log, and this one needed debugging from a build log.
print(f"[offband-patch] version base: describe={base_tag}; "
      f"beta_* tags local={len(_tags)} ancestors={len(_ancestors)}")

if _ancestors:
    def _tagkey(t):
        # beta_9 must sort below beta_85, so compare numerically, not as strings.
        _n = t[len("beta_"):]
        return (0, int(_n)) if _n.isdigit() else (1, 0)
    _newest = max(_ancestors, key=_tagkey)
    if _newest != base_tag:
        _die(
            f"version base is ambiguous: `git describe` says '{base_tag}' but the newest "
            f"beta_* tag that is an ancestor of HEAD is '{_newest}'.\n"
            f"  beta_* tags present: {len(_tags)}; of those, ancestors of HEAD: {len(_ancestors)}\n"
            f"  describe would stamp: {base_tag}+ob."
            f"{_git('rev-list', '--count', f'{base_tag}..HEAD')}\n"
            f"  newest ancestor says: {_newest}+ob."
            f"{_git('rev-list', '--count', f'{_newest}..HEAD')}\n"
            "Usually an incomplete tag fetch. Try: git fetch --tags --force"
        )

# The check above compares describe against the tags we HAVE. It cannot catch the
# case that actually bit CI (#92): the newest tag missing from the clone entirely,
# so that the newest tag we have and describe's answer agree — on beta_44 — and
# nothing looks wrong. Only the remote knows what we should have.
#
# One `ls-remote` per build, and it is advisory about the network but strict about
# the answer: an unreachable remote passes with a warning (offline builds are
# legitimate), a reachable remote that lists a newer beta_* tag than our newest
# ancestor FAILS. The point is that "I could not check" and "I checked and it is
# wrong" must not look the same, which is the mistake this whole guard exists to
# stop repeating.
try:
    _ls = subprocess.run(["git", "ls-remote", "--tags", "origin", "refs/tags/beta_*"],
                         capture_output=True, text=True, check=False, timeout=30)
except (subprocess.SubprocessError, FileNotFoundError, OSError) as _e:
    _ls = None
    sys.stderr.write(f"[offband-patch] WARNING: could not list remote tags ({_e}); "
                     f"version base '{base_tag}' not cross-checked against origin.\n")
if _ls is not None and _ls.returncode != 0:
    sys.stderr.write("[offband-patch] WARNING: `git ls-remote` failed "
                     f"(rc={_ls.returncode}: {_ls.stderr.strip()[:120]}); "
                     f"version base '{base_tag}' not cross-checked against origin.\n")
elif _ls is not None:
    _remote = []
    for _line in _ls.stdout.splitlines():
        _parts = _line.split("refs/tags/")
        if len(_parts) == 2 and not _parts[1].endswith("^{}"):
            _remote.append(_parts[1].strip())
    # Only tags we could actually be based on: those that are ancestors here. A tag
    # we do not have cannot be tested for ancestry, so missing-and-newer is the
    # signal, and `base_tag` is our floor for "newer".
    _base_n = base_tag[len("beta_"):]
    if _base_n.isdigit():
        _newer_missing = sorted(
            (t for t in _remote
             if t[len("beta_"):].isdigit()
             and int(t[len("beta_"):]) > int(_base_n)
             and t not in _tags),
            key=lambda t: int(t[len("beta_"):]),
        )
        # Newer tags that we DO have but that are not ancestors are fine and expected
        # (upstream moved on; we have not taken it yet). Only a tag we are missing
        # outright means the clone cannot answer the question it was just asked.
        if _newer_missing:
            _die(
                f"incomplete tag set: origin has {len(_newer_missing)} beta_* tag(s) newer "
                f"than '{base_tag}' that this clone does not have "
                f"({', '.join(_newer_missing[:5])}"
                f"{', ...' if len(_newer_missing) > 5 else ''}).\n"
                f"  local beta_* tags: {len(_tags)}; origin: {len(_remote)}\n"
                f"  so '{base_tag}+ob."
                f"{_git('rev-list', '--count', f'{base_tag}..HEAD')}' may name the wrong base.\n"
                "Fetch the tags and rebuild: git fetch --tags --force\n"
                "In GitHub Actions, set `fetch-tags: true` on actions/checkout."
            )
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
# The generated header itself is gitignored (#28) -- NOT the whole scripts/build/
# dir, which also holds tracked scripts (add-emoji.py, gen-flasher-meta.py,
# gen-qr-icon.py, patch_radiolib_lr11x0.py). Keep this file untracked: it is
# board-specific and rewritten every build, so tracking it kept the tree
# permanently dirty and forced a false '+d' onto FIRMWARE_VERSION via the
# `git status --porcelain` check above.
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
