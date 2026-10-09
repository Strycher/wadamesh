# Feature #68 — break off from upstream wadamesh: outcomes

Status: **awaiting owner sign-off.** Epic E (#80) closes on review, not on an
agent's say-so.

## In plain language

We used to track upstream wadamesh as a fork and carry our changes as a pile of
branches that each sat on whatever upstream tag happened to be current when the
work started. That made every catch-up a rebase of our work onto a moving base,
and it made "what have we actually changed?" an unanswerable question.

Now `main` is our own trunk. Upstream is a vendor we merge *from*, one tag at a
time, and our work lands on top through ordinary pull requests. The practical
difference: a catch-up is now a merge of a few dozen upstream commits instead of
a replay of our history onto a new base.

Three pieces of our own work that had been stranded on old bases are back on the
trunk — the V4/R8 battery and sensor fixes, the Wi-Fi/BLE coexistence fix and
the Windows build tooling, and the CrowPanel7 (ESP32-P4) port. Six boards now
build from one tree.

Nothing in this Feature was tested on hardware. It is a build-and-structure
result, not a behaviour result.

## What was delivered

| Epic | What | Lands as |
|------|------|----------|
| A (carried in) | Partition CSVs made ASCII-only; RadioLib LR11x0 drift guarded | #75, #76 |
| #70 | Trunk cut-over: left the fork network, remotes renamed, `main` to `beta_85`, branch protection | PR #74, repaired by `7964140` |
| B (#77) | V4/R8 battery ADC, I2C driver-collision detector, boot reset-reason logging | PR #85, merged `eadcad7` |
| C (#78) | Wi-Fi/BLE coexistence fix, Windows build tooling, generated-header hygiene | PR #87, merged `202dced` |
| — | CI build correctness: pinned toolchain, honest version guard (#92, #93) | PR #94, merged `74e2ba2` |
| D (#79) | CrowPanel7 (ESP32-P4) port: board target, shared-tree wiring, behaviour, CI entry | PR #89, merged `52cd074` |
| E (#80) | This document | this PR |

Merges are rebase, one Epic at a time, each rebased onto the `main` the previous
one produced. `git log --first-parent main` therefore gains one entry per commit
and no merge commits — see §4.

## Evidence

### 1. Six boards build from one tree

Six boards, one tree, **green in CI** on the PR that put Epic D onto `main` —
not only on a developer machine:

```
crowpanel7_companion_radio_touch                pass  8m44s   RAM 14.4%   Flash 74.7%  (3,036,598)
heltec_v4_tft_companion_radio_usb_tcp_touch     pass  2m26s   RAM  5.1%   Flash 95.6%  (3,882,493)
heltec_v4_r8_tft_companion_radio_usb_tcp_touch  pass  3m09s   RAM  1.4%   Flash 85.8%  (3,485,973)
LilyGo_TDeck_companion_radio_touch              pass  2m31s   RAM 36.4%   Flash 84.0%  (3,414,397)
ThinkNode_M9_companion_radio_touch              pass  2m12s   RAM 35.9%   Flash 83.4%  (3,388,185)
rak_tap_v2_companion_radio_touch                pass  3m02s   RAM  1.2%   Flash 81.4%  (3,306,833)
```

The P4 also produces `firmware.bin` + `firmware.factory.bin` (bootloader at
`0x2000`).

**The CrowPanel7 flash figure is byte-identical to the local build** —
3,036,598 either side. That is worth more than a green tick: after the toolchain
pin (§"Things found", #93) CI and the developer machine produce the same binary,
which is the property that makes a cloud build mean anything at all.

**Worth flagging:** `heltec_v4_tft` is at **95.6% of its app slot**, ~180 KB of
headroom. That is the daily driver and the tightest board in the fleet. It is
not a regression from this Feature — Epic C added ~1.8 KB — but it is close
enough that the next feature on that board should expect to pay for itself.

**`crowpanel7` is in the CI matrix but NOT a required status check**, and must
stay that way until it has been green across several merges. One green run is
not a track record, and a required context that cannot report blocks every PR
forever.

**Worth flagging:** `heltec_v4_tft` is at **95.6% of its app slot**, ~180 KB of
headroom. That is the daily driver and the tightest board in the fleet. It is
not a regression from this Feature — Epic C added ~1.8 KB — but it is close
enough that the next feature on that board should expect to pay for itself.

### 2. `main` is PR-only and protection binds everyone

From the GitHub API:

```
protected          : true
enforcement_level  : everyone
required checks    : build (heltec_v4_tft_companion_radio_usb_tcp_touch)
                     build (heltec_v4_r8_tft_companion_radio_usb_tcp_touch)
                     build (LilyGo_TDeck_companion_radio_touch)
                     build (ThinkNode_M9_companion_radio_touch)
                     build (rak_tap_v2_companion_radio_touch)
```

Five checks, matching exactly the five envs CI builds. `crowpanel7` is
deliberately **absent** — it is in the CI matrix but must not become a required
context until it has reported successfully several times, because a required
check that never reports blocks every PR permanently.

The PR-requirement and the linear-history setting are not readable at the
automation account's token scope (that endpoint returns 404 for it, which is a
permissions boundary and not an absence of protection). Both were set and
confirmed by the owner on 2026-09-30 and are recorded in the project notes.

### 3. `beta_85` ancestry, and the delta property — the point of the Feature

```
commits in beta_85 not in main      0
merge-base(main, beta_85)           == beta_85
```

`beta_85` is wholly contained in `main`. That is the property that makes a
future upstream take a delta: everything up to `beta_85` is already present, so
a merge of a later tag can only bring commits that postdate it.

What that saves, using the most recent real tag pair as the worked example:

```
beta_84 -> beta_85            28 commits,  39 files,  +3,209 / -271
total history in beta_85   1,180 commits
```

So a take is tens of commits, not a four-figure replay.

**Demonstrated against live upstream content**, with the owner's approval, on
2026-10-02. Upstream is `ALLFATHER-BV/wadamesh`; no remote was added and nothing
was merged — a read-only fetch into a temporary ref, a non-destructive
`git merge-tree`, then the ref deleted.

First, the take was faithful. Our tag and upstream's are the same object:

```
our      beta_85   df99548a10377fd7eca81ae80d200f91591e3cc6
upstream beta_85   df99548a10377fd7eca81ae80d200f91591e3cc6
```

`beta_85` is still upstream's newest tag, so the test ran against upstream's
current `main`, which has moved past it:

```
merge-base(main, upstream/main)   df99548a  == beta_85 exactly
commits a take would bring        1
                                  c9ecaf3 "reports: say how long they ran it,
                                           not just a span"
diffstat                          2 files, +6 / -3
                                  deploy/report-service.py, deploy/site/beta.html
commits we have that upstream does not   6
dry-run merge (git merge-tree)    CLEAN — exit 0, no conflicts, tree 90e7e0a3
```

**The merge base is precisely the tag we took.** That is the whole property, and
it is now a measured fact rather than an inference: a take brings only what
upstream added after `beta_85`, it applies without conflict, and it does not
disturb the six commits of our own that upstream has never seen.

One honest caveat on the size: the delta happens to be a single commit because
upstream has pushed little since `beta_85` (last push 2026-09-23). The figure to
plan against is the `beta_84 -> beta_85` shape above — tens of commits — not this
one. What the test establishes is the *mechanism*, not a forecast of volume.

### 4. `git log --first-parent main`

Measured on `main` **after** all four Epics landed:

```
merge commits on first-parent main, total        12
of those, created since the break-off point       1   (7964140, the beta_85 ancestry record)
inherited from upstream's imported history       11
our own commits on first-parent since break-off  14
```

The rule — only an upstream take ever creates a merge commit, our own work never
does — holds, and this Feature is the first real test of it: fourteen commits of
our own work went onto `main` and added **zero** merge commits, because every
Epic landed by rebase. The eleven inherited merges are upstream's own PR merges
from before the cut-over and cannot be rewritten without discarding the shared
history.

One scar is visible and worth knowing about. `40b1fba`
("trunk cut-over — merge upstream `beta_85` into `main`") appears on the
first-parent line but is **not** a merge commit: GitHub squashed it, which
silently discarded the second parent and with it the ancestry the merge existed
to create. `7964140` repaired that afterwards with a tree-preserving
`git merge -s ours`. The lesson is recorded in the project notes: never route a
vendor tag through a PR branch — merge it on `main` directly.

## Things found along the way that were not in the plan

These were defects in the work being replayed, caught because the replay forced
every hunk to be read against the new trunk:

- **One board was on a different core.** The CrowPanel7 env was still pinned to
  `core-v1.16.5` while every other env had moved to `core-v1.17.4`. The shared
  `src/` no longer compiles against the older core (`DisplayDriver::Color` became
  a `ColorVal` alias). One board on its own core is not one codebase.
- **The thread-metadata writer had lost its commit step.** Merged naively, every
  write would have landed in `threads.bin.tmp` and the live table would never
  have changed — a silent, total loss of thread metadata on reboot.
- **An SD write was heading for the wrong core.** Upstream had moved the
  chat-history flush off core 0 on measured evidence (the Wi-Fi driver starves a
  low-priority task, and `sd_diskio` busy-waits on wall time, so healthy SD
  writes read as EIO). The replayed thread flush was the same kind of write and
  was moved to match.
- **A generated header was never ignored.** The ported `inject_version.py`
  arrived asserting that `scripts/build/` was gitignored. True where it came
  from, false here, so every build left the tree dirty.
- **A `/docs/` ignore would have swallowed the plans of record.** The CrowPanel7
  branch ignored all of `/docs/` for vendor reference material; it predates
  `docs/initiatives/`. Narrowed to `docs/hardware/`.

### And two that only CI could find

Worth separating, because these were invisible to every local build and are the
clearest argument this Feature produced for putting the envs in front of CI at
all. Both are fixed; both are the kind of thing that stays fixed only if written
down.

- **41 upstream tags had never been pushed to our own remote** (#92).
  `beta_45`..`beta_85` existed in the developer clone and not on `origin`, whose
  newest `beta_*` tag was `beta_44`. Every CI build therefore stamped
  `beta_44+ob.869` while the same commit gave `beta_85+ob.11` locally — and CI
  was *right*, given what the repository could actually answer. Nothing local
  can detect this: the clone is complete, the history is complete, only the
  remote is short. Fixed by pushing the tags (origin now has 83).

  The lesson is not about tags. `inject_version.py` already refused to build on a
  shallow clone, on the principle that a version it cannot determine must fail
  rather than be guessed. This was the same principle defeated one level down: it
  *could* determine a version, and the version was wrong. The guard now
  cross-checks `describe` against the newest present ancestor and against
  `origin`'s tag list, treats a `merge-base` error as an error rather than as
  "no", and prints one line saying what it concluded — because a guard whose
  success is silent cannot be debugged from a build log, and this one had to be.

- **CI installed a different toolchain than the developer machine** (#93).
  `pip install --upgrade platformio` pulled a core requiring
  `tool-scons ~4.41101.0`, which installed over the 4.8.1 that pioarduino's
  platform pins; SCons 4.11 removed `SCons.Tool.FortranCommon`, which the espidf
  builder imports, so CrowPanel7 compiled the whole tree and died at link. Now
  `platformio==6.1.19`. Never CrowPanel7-specific — an unpinned core would have
  reached the other five envs in time.

## What is left

**Settled by the owner, 2026-10-02:**

- CrowPanel7 **stays in the build matrix**, and therefore keeps its
  `wadamesh-crowpanel7` entry in `scripts/release.sh` — the `WADA_BOARD_ID`
  guard requires the board-id table and the release list to move together. A
  manual `release.sh` run will publish an artifact for a board that has not yet
  been flash-verified; that is accepted, and nothing publishes without someone
  cutting a tag and running the script.
- The upstream dry run was approved and run. Result in §3.

**Deferred, with issues:**

- #84 — the SHTC3 bounded-read fix must be hand-ported; `beta_85` restructured
  `refreshStatusLabels` so the diff will not replay, and it cannot be split from
  the trace instrumentation it depends on.
- #76 — ThinkNode M9 LR11x0 old-firmware story, now that RadioLib gates it
  upstream. Backlogged.
- #57 — reconcile the flash harness and hardware registry.
- Feature #69 — Offband parity on key capabilities, to be named by the #44
  inventory.

**Not done at all, by design:**

- **No hardware testing.** No device was flashed in this Feature. Every claim
  above is a build or a repository-structure claim. The coexistence fix in
  particular is unverified on the new base: it built and its reasoning holds, but
  nobody has watched a node bring Wi-Fi and BLE up on it.
## How the stack was actually landed

Written down because the mechanics were not obvious and the next stacked Feature
will hit all of it again.

**A stacked PR gets no CI.** `ci.yml` triggers on
`pull_request: branches: [main]`, so while Epics C, D and E targeted each other
they ran the PR-body check and nothing else. Every Epic was therefore merged one
at a time, each one retargeted to `main` and given a real matrix run first. The
local build figures were only ever interim evidence; §1 is now CI's.

**Retargeting does not fire CI either.** `ci.yml` declares no `types:`, so
`pull_request` defaults to `[opened, synchronize, reopened]`. Changing a base
fires `edited`, which is not in that set, and the PR sits at
`MERGEABLE/BLOCKED` with required checks that can never report. Each Epic had to
be closed and reopened to get a run.

**Each Epic needed a rebase, not just a retarget.** Merges here are rebase, so
landing one Epic rewrites the SHA the next one sits on, and the branch above
carries a stale copy of a change `main` already has. `git rebase origin/main`
dropped those by patch-id every time (`warning: skipped previously applied
commit ...`), and the tree came out identical each time — only the SHAs moved.

That is three separate ways a stacked chain can look fine and not be, none of
them visible from the branch itself.

## Licence note

Upstream wadamesh is **GPL-3.0-or-later**, Kaj Schittecat and contributors.
Diverging, publishing, and never contributing back are all permitted; `LICENSE`
and `NOTICE` stay intact and this derivative stays GPL-3.0. This is not the same
freedom the project has over its own code, and a genuinely DifferentWire-owned
firmware would have to be clean-room rather than this repository renamed.
