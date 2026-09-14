# Browser E2E harness

Real `fcitx5` session under Xvfb; keys injected via `xdotool` (XTEST); results
asserted in Chromium/Firefox DOM via Playwright. What this lane guards is the
key-to-DOM plumbing in `Mode=Preedit` (XTEST → fcitx5 → XIM/GTK path → browser
→ committed text). Engine logic (Telex/VNI rules, macro, per-app mode rules,
surrounding text) is guarded by the headless ctest suite in `test/` —
deliberately NOT here, and the workflow header records the same scope.

## Invariants (violating any of these produces green-but-meaningless runs)

- The harness owns its X display: `DISPLAY` is never inherited, the run aborts
  if the display is occupied (live server or stale socket), and readiness polls
  `xdpyinfo` (hard dependency — no silent socket-only fallback).
- `HOME`/`XDG_*` are exported BEFORE openbox starts, and openbox readiness
  waits for `_NET_SUPPORTING_WM_CHECK` — the focus/raise policy in `rc.xml` is
  load-bearing for `ensureActive()`; without the ordering, tests pass on
  stock-default luck.
- D-Bus is a private bus from `dbus-session.conf` (no
  `standard_session_servicedirs`) — fcitx5 name ownership (`-r`) is scoped to
  it, so the harness can coexist with a developer's running desktop.
- `workers=1`/`fullyParallel=false`: one display, one focused window. Parallel
  keystroke injection into the same X server is undefined behavior, not speed.
- `retries=0`: red runs are data. Do not add retries; open a triage issue.
- `fcitx5` comes from apt and its version is printed per run — a green run
  only attests to the (addon SHA × fcitx5 version × runner image) triple it
  actually executed.

## CI gate contract

Lives in `.github/workflows/browser-e2e.yml` header; summary: PRs are gated on
harness changes only; nightlies + dev pushes carry the engine-facing signal.
Promotion to a blocking `src/**` gate needs ≥20 consecutive clean nightlies
with ≥5 uncontaminated by `src/**`/`bamboo/**` changes; demotion on 3 reds
without a linked triage issue, or >1 failure per rolling 10.

## Local use (Linux/X11 only)

```bash
scripts/run-browser-e2e.sh          # creates TEST_HOME, starts stack, runs both browsers
BROWSER_E2E_DISPLAY=:98 scripts/run-browser-e2e.sh   # if :99 is taken
```

Triage on red: the failure artifact carries `input-events.json` (DOM-side),
`fcitx5.log` at `*=4` (inter-event timing), xvfb/openbox logs, and
`/proc/pressure` — high cpu pressure + failed poll ⇒ runner statistics event,
not a Lotus regression. The json reporter timing artifact (`*.json`) is
uploaded on green runs too; compare p95 poll margins against the 2 s
`expect.poll` deadlines before blaming the engine.
