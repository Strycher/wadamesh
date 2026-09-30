# Feature PLAN: break off from upstream wadamesh

| | |
|---|---|
| **Feature** | [#68](https://github.com/Strycher/wadamesh/issues/68) · Citadel `wadamesh-07b` |
| **Serves** | No Initiative parent. Two Features stand alone: this one and [#69](https://github.com/Strycher/wadamesh/issues/69) (Offband parity), which depends on it. |
| **Status** | Draft. Approved when this PR merges. |
| **Owner** | @Strycher |
| **Last updated** | 2026-09-27 |

## At a glance

- **What:** `Strycher/wadamesh` becomes our own trunk, with the normal DW workflow — issue → Epic branch → PR → merge to `main` — and no intent to submit upstream.
- **Why now:** nothing has ever landed on `main`. Every line of work is a long-lived branch off a different frozen upstream tag, and `main` is 850 commits behind. That is the root cause behind #54, #57 and #67, not three separate problems.
- **How:** establish the trunk first (Epic A), then land the existing work onto it as Epic PRs (B, C, D), then prove the whole thing (E).
- **Done when:** `main` carries beta_85 plus our work, is protected, and a PR is the only way in.

## 1. Diagnosis

| # | Finding | Evidence | Effect |
|---|---|---|---|
| 1 | `main` has never received work. It sits 850 behind upstream and 2 ahead. | `git rev-list --left-right --count beta_85...origin/main` → `850 / 2`, 2026-09-25 | Every branch bases off a different frozen tag; "rebase" recurs forever instead of being a one-time merge. |
| 2 | Our 2 `main` commits are the **DW governance workflows** — board sync, auto-promote, branch-cleanup, incident-autoclose guard. | `git log --oneline beta_85..origin/main` → `ab805d0`, `f68a868` | A fast-forward to beta_85 would silently delete the automation that makes the workflow work. **This is why the move is a merge.** |
| 3 | Merging `beta_85` into `main` is conflict-free. | `git merge-tree --write-tree --name-only origin/main beta_85` → exit 0, no conflicted paths, tree `131b18fb` | The merge is mechanical; the risk is in what lands after it, not the merge. |
| 4 | `Strycher/wadamesh` is a real GitHub fork of `ALLFATHER-BV/wadamesh`, and no `gh` default repo was set. | `gh api repos/Strycher/wadamesh` → `fork=true`, `parent=source=ALLFATHER-BV/wadamesh`; `gh repo set-default --view` → unset | `gh pr create` aimed at upstream. This is the friction that started the Feature. |
| 5 | Remotes were inverted vs every other DW repo: `origin` = upstream, `fork` = ours. | `git remote -v`, 2026-09-25 | One slip pushes at upstream. |
| 6 | No branch protection on `main`. | `gh api repos/Strycher/wadamesh/branches/main/protection` → 404 | Nothing enforces the PR route. |
| 7 | Work is stranded on two bases: `beta_36` (`test-beta36` +2, `epic/2-crowpanel7-port` +32) and `beta_61` (`epic/54-upstream-rebase-v4r8` +3, `fix/58-v4-battery-adc` +8). | `git rev-list --left-right --count` per branch, 2026-09-25 | The R8/battery/trace work cannot be reached from the board Ben runs. |
| 8 | Licence is GPL-3.0-or-later, Kaj Schittecat and contributors. | `LICENSE`, `NOTICE` | Diverging and never contributing back is fine; relicensing is not. A DifferentWire-owned firmware must be clean-room (#42/#43) — it cannot be this repo renamed. |

## 2. Scope

**In:**
- Trunk establishment: remotes, `gh` default, `main` = merge of `beta_85`, branch protection.
- Landing the existing branches onto the new `main` as Epic PRs, in dependency order.
- Correcting `CLAUDE.md` and #68 where they still assert a fast-forward.

**Out:**
- Offband parity and the `pio-flash` uplift — Feature #69, which depends on this one.
- The registry reconcile (#57) — belongs under #69 with the tool port.
- Any device flash. No Epic here flashes anything.
- Detaching the GitHub fork network. Not needed; a fork can own its trunk, PRs and protection.

## 3. Epics

Tasks are 3–4 points. Each Epic ends with a verification task and gets one PR.

### Epic A: trunk cut-over — [#70](https://github.com/Strycher/wadamesh/issues/70) · `wadamesh-g82`

| Task | Pts |
|---|---|
| **A1 · Remotes and `gh` default:** `origin` = Strycher, `upstream` = ALLFATHER-BV with push URL `no-push`, `gh` default repo set, `main` tracking `origin/main`. Checked by `git remote -v` and `gh repo set-default --view`. **Already done — see §9.** | 3 |
| **A2 · Merge `beta_85` into `main` by PR.** Never a direct push: `require-merge-grant.py:397` — a direct push to the default branch can never be covered by a grant. Checked by `git log --merges -1 main` and `git merge-base --is-ancestor beta_85 main`. | 3 |
| **A3 · Branch protection on `main`:** require a PR. Checked by `gh api repos/Strycher/wadamesh/branches/main/protection` returning `required_pull_request_reviews`. | 3 |
| **A4 · Epic verification:** `gh pr create` from a scratch branch targets Strycher not ALLFATHER-BV; a direct push to `main` is refused; `main` contains `beta_85`; and the named command below is green on the new `main`. | 1 |

**Epic A is DONE** (2026-09-30). `main` is at `7964140`; `beta_85` is an ancestor; protection binds everyone with five required checks. One scar worth keeping: A2 was landed as a **squash**, which discarded the ancestry, and had to be repaired with a tree-preserving `merge -s ours`. Cause: the upstream merge was carried on a PR branch. **A vendor tag is merged on `main` directly, never through a PR** — recorded in `CLAUDE.md`.

### Epic B: land the R8 / V4 work — [#77](https://github.com/Strycher/wadamesh/issues/77) · `wadamesh-6vu`

| Task | Pts |
|---|---|
| **B1 · Replay `fix/58-v4-battery-adc` (8 commits) onto the new `main`.** Carries the battery-ADC fix, the schematic-verified 4.9 divider (V4 **and** V4-R8), the bounded SHTC3 read with env sampling off the UI thread, WmTrace breadcrumbs, and the I2C driver-collision detector. Supersedes epic #54, currently `board:testing` on a base now 508 behind. | 4 |
| **B2 · Confirm the R8 env exists and builds** (`heltec_v4_r8_tft_companion_radio_usb_tcp_touch`), and correct its stale "UNTESTED" comment. | 3 |
| **B3 · Epic verification:** both envs build; no device flash. | 1 |

### Epic C: land coex + Windows build tooling — [#78](https://github.com/Strycher/wadamesh/issues/78) · `wadamesh-0q2`

| Task | Pts |
|---|---|
| **C1 · Re-apply the two `test-beta36` commits** — Windows build tooling (`force_tempfile`, `inject_version`) and the WiFi-first BLE ordering with heap-guarded `WiFi.begin`. Decide per-hunk whether upstream has since superseded the coex work. | 4 |
| **C2 · Epic verification.** | 1 |

### Epic D: land CrowPanel7 — [#79](https://github.com/Strycher/wadamesh/issues/79) · `wadamesh-qlj`

| Task | Pts |
|---|---|
| **D1 · Replay `epic/2-crowpanel7-port` (32 commits) onto the new `main`.** Highest conflict surface — `UITask.cpp`, `MyMesh.*`, `main.cpp` SD/LDO4 ordering, `DataStore.cpp`. Resolution needs CrowPanel7 intent; see the hand-off notes on #2. | 4 |
| **D2 · Epic verification:** `crowpanel7_companion_radio_touch` builds green. | 1 |

### Epic E: Feature Integration Testing — [#80](https://github.com/Strycher/wadamesh/issues/80) · `wadamesh-jul`

| Task | Pts |
|---|---|
| **E1 · Prove the integrated trunk:** all three envs build from one `main`; PR-only route enforced; upstream merge repeatable. | 4 |
| **E2 · `OUTCOMES.md`** beside this plan. | 1 |

## 4. Order and dependencies

A is the base capability — nothing can land until `main` exists and is protected. B before C before D by ascending conflict surface: B and C are small hunks, D is the 32-commit CrowPanel7 replay across the two largest files in the tree. E depends on all of A–D.

File convergence: C and D both touch `main.cpp` and `UITask.cpp`, so they must be sequential, not parallel. B touches `variants/heltec_v4/` and `device_caps.h`, which neither C nor D does.

## 5. Grants and authorization

What merging this plan authorizes, in words:

| Term | Proposed | Why |
|---|---|---|
| **What merging this plan authorizes** | Epics B (#77), C (#78), D (#79) and E (#80), in that order | Epic A (#70) is already merged. B–E now have issues, so the chain covers the rest of the Feature. |
| **Merges** | 4 | Basis: one PR each for B, C, D and E. Epic A consumes nothing — it is done. This plan-update PR is **not** in the budget: a plan cannot authorize its own merge, so it takes a one-off merge token. |
| **Epic verification** | `command` mode, running the named command below | The acceptance bar for every Epic here is "the HV4.3 env still builds". A command the hook runs on the PR's exact commit is stronger than a human eyeballing it, and it does not interrupt you. See §8 Q1 to override. |
| **Owner steps during the run** | Three, total: (1) a one-off merge token for **this** PR, (2) mint the plan grant once it is merged, (3) re-mint to extend the chain once B–E have issues | There is no way to avoid (1): a plan cannot authorize its own merge. The exact invocation is handed to the owner privately, not recorded here. |
| **Stops** | A failed verification breaks the chain until you re-mint. A used-up budget pauses the run and I ask, with the reason. | |
| **Flash** | `null` | No Epic in this Feature flashes a device. The R8 has still never been flashed; that is deliberately out of scope. |

```grant-terms
{
  "epic_chain": [70, 77, 78, 79, 80],
  "chain": true,
  "verification": { "mode": "command", "command": "pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch" },
  "merge": { "budget": 4, "basis": "one PR each for Epics B (#77), C (#78), D (#79) and E (#80). Epic A (#70) is already merged and consumes nothing. This plan-update PR is excluded, as a plan cannot authorize its own merge." },
  "flash": null,
  "reset": null,
  "expires_after_hours": 168
}
```

## 6. Verification

- **Named command:** `pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch` — the HV4.3 env, the board Ben actually runs.
- **Acceptance bar:** the HV4.3 env builds green on the new `main` at every Epic boundary. Epic D additionally builds `crowpanel7_companion_radio_touch`; Epic B additionally builds `heltec_v4_r8_tft_companion_radio_usb_tcp_touch`. No Epic is accepted on a build that was not run and read.

## 7. Done when

- `main` contains `beta_85` **and** the two governance-workflow commits — `git merge-base --is-ancestor beta_85 main` passes and `.github/workflows/` is intact.
- Branch protection on `main` requires a PR; a direct push is refused.
- `gh pr create` targets `Strycher/wadamesh`.
- All three board envs build from a single `main`.
- Pulling upstream is a repeatable merge, not a branch replay.
- `OUTCOMES.md` exists and you have approved the results.

## 8. Your choices

| # | Question | Option A | Option B | Rec. | Your choice |
|---|---|---|---|---|---|
| 1 | How is each Epic verified before its merge? | `command` mode — the hook runs `pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch` on the PR's exact commit. Fewer interruptions. | `owner` mode — you approve each Epic's PR with `dw-approve verify`. You see every Epic. | ⭐ A (set in the block above) | |
| 2 | The 4 local changes I made before this plan existed (§9). | Declare them as carried-in and move on. | Revert, then redo them under Epic A. | ⭐ A — **you chose this on 2026-09-27** | A |
| 3 | Does Epic D (CrowPanel7, 32 commits) belong in this Feature? | Keep it here — one trunk, everything lands. | Split into its own Feature; this one ends at Epic C. | ⭐ A, but D is by far the largest risk and splitting it is defensible | |

## 9. Carried-in work

Executed **before this plan existed**, which was out of order — the plan is the authorization, and it should have come first. All of it is local to this host, nothing was pushed, no PR was opened and no merge happened. Declared here rather than reverted, per your decision of 2026-09-27.

| Item | Goes to |
|---|---|
| `origin` renamed to `upstream`; `fork` renamed to `origin` | Epic A, task A1 |
| `upstream` push URL set to `no-push` | Epic A, task A1 |
| `gh` default repo set to `Strycher/wadamesh` | Epic A, task A1 |
| local `main` tracking repointed from `upstream/main` to `origin/main` | Epic A, task A1 |
| Epic A's issue #70 and Citadel `wadamesh-g82` created ahead of this plan | Epic A — the grant's `epic_chain` needs the number, so it stays |
| Device registry pinned to a per-host state directory outside every checkout, resolved by `scripts/wadamesh_state.py` | Feature #69 — logged on its issue; not part of this Feature |
| `CLAUDE.md` and #68 still assert a fast-forward | Epic A, task A2 — corrected as part of it |

Rollback for the four A1 items, if you change your mind: rename the remotes back, restore `origin`'s push URL, `git branch --set-upstream-to=upstream/main main`, `gh repo set-default --unset`.

When this plan merges, each remaining Epic and Task gets a GitHub issue and a Citadel task, per canon.
