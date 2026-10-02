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
| B (#77) | V4/R8 battery ADC, I2C driver-collision detector, boot reset-reason logging | PR #85 |
| C (#78) | Wi-Fi/BLE coexistence fix, Windows build tooling, generated-header hygiene | PR #87 |
| D (#79) | CrowPanel7 (ESP32-P4) port: board target, shared-tree wiring, behaviour, CI entry | PR #89 |
| E (#80) | This document | — |

## Evidence

### 1. Six boards build from one tree

Built from the integrated branch head (`main` + Epics B, C, D). These become
`main` itself when the stack merges in Epic order.

```
crowpanel7_companion_radio_touch                SUCCESS   RAM 14.4%   Flash 74.7%  (3,036,598)
heltec_v4_tft_companion_radio_usb_tcp_touch     SUCCESS   RAM  5.1%   Flash 95.6%  (3,882,493)
heltec_v4_r8_tft_companion_radio_usb_tcp_touch  SUCCESS   RAM  1.4%   Flash 85.8%  (3,485,973)
LilyGo_TDeck_companion_radio_touch              SUCCESS   RAM 36.4%   Flash 84.0%  (3,414,397)
ThinkNode_M9_companion_radio_touch              SUCCESS   RAM 35.9%   Flash 83.4%  (3,388,185)
rak_tap_v2_companion_radio_touch                SUCCESS   RAM  1.2%   Flash 81.4%  (3,306,833)
```

The P4 also produces `firmware.bin` + `firmware.factory.bin` (bootloader at
`0x2000`).

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

### 3. `beta_85` ancestry, and the delta property

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

**This is the one property that is NOT fully demonstrated, and it is the one the
Feature exists to establish.** `beta_85` is the newest upstream tag in this
clone, and there is no `upstream` remote — it was removed on leaving the fork
network. A genuine dry run needs a tag that postdates `beta_85`, fetched from the
upstream repository, and contacting upstream is an owner-approval action under
this project's own rules. The property is therefore shown **by construction from
the commit graph** and not yet **exercised against a real later tag**.

What would close it, when the owner chooses to: fetch a tag past `beta_85`
read-only, then on a throwaway branch off `main` run a no-commit merge of it and
record the commit count and diffstat. Expected result is a delta on the order of
the `beta_84 -> beta_85` figures above. Until that is run, treat the delta
property as reasoned rather than proven.

### 4. `git log --first-parent main`

```
merge commits on first-parent main, total        12
of those, created since the break-off point       1   (7964140, the beta_85 ancestry record)
inherited from upstream's imported history       11
```

The rule — only an upstream take ever creates a merge commit, our own work never
does — holds for everything after the break-off. The eleven inherited merges are
upstream's own PR merges from before the cut-over and cannot be rewritten
without discarding the shared history.

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

## What is left

**Needs an owner decision:**

- `scripts/release.sh` gained a `wadamesh-crowpanel7` ENVS entry, because the
  `WADA_BOARD_ID` guard requires the board-id table and the release list to stay
  in sync. A manual `release.sh` run would therefore publish an artifact for a
  board that has never been flash-verified. CI is unaffected. Removing the line
  is a one-liner if the board should stay build-only until it is verified.
- Whether to run the real upstream dry run described in §3.

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
- **CI did not run on the stacked PRs.** `ci.yml` triggers on
  `pull_request: branches: [main]`, so Epics C, D and E get only the PR-body
  check while they target each other. The full matrix runs — and the required
  checks bite — once Epic B merges and GitHub retargets the stack to `main`. The
  build figures in §1 are the interim evidence and were produced locally.

## Licence note

Upstream wadamesh is **GPL-3.0-or-later**, Kaj Schittecat and contributors.
Diverging, publishing, and never contributing back are all permitted; `LICENSE`
and `NOTICE` stay intact and this derivative stays GPL-3.0. This is not the same
freedom the project has over its own code, and a genuinely DifferentWire-owned
firmware would have to be clean-room rather than this repository renamed.
