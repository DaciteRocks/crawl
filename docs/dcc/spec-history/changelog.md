# DCC spec change log

## 2026-10-25 — Phase 0 done

Landed: the repo-root `CLAUDE.md` holds the gate, the `nul` rule, the isolation
and no-trademark rules, and the optional Debug Console route. `nul` is now in
`crawl-ref/source/.gitignore`. Findings are in `discovery.md`. The Standard
gate worked as written: Release-build `-builddb` and `-script` both run
headless. The review found no bugs. Reviewers asked for three doc cleanups,
all applied:
- `CLAUDE.md` now states the gate path once (`SRC`/`MSBUILD`), and SPEC.md
  names `CLAUDE.md` as the authoritative copy of the gate.
- The `script error:` text now cites its source in `ctest.cc` and is marked
  as observed behaviour.
- `discovery.md` now records why VS2022 is not used.

## 2026-10-25 — Phase 1a done

Landed: `GAME_TYPE_DCC` ("Crawl Live") is appended before `NUM_GAME_TYPE`.
`game_is_descent()` also returns true for it, and `game_is_dcc()` exists. It
shows in the startup menu in every build, saves to `dcc/`, can be picked with
rc `type = dcc` or `-dcc`, and uses the Descent tile. Reviewers changed:
- added a `welcome_spam crawl live` entry to `miscname.txt` (the start-of-game
  line was blank);
- merged the DCC tile case into the Descent case;
- added `-dcc` to the `-help` text (`main.cc`) and `dcc` to the `type` entry in
  `options_guide.txt`;
- added a "Merging upstream" section to `CLAUDE.md` about game-type numbering in
  saves.
Kept on purpose: DCC is not alpha-gated, the tourney prompt at `newgame.cc:430`
is untouched, and `dcc` is listed in `-gametypes-json`.

## 2026-10-25 — Phase 1b done

Landed: `source/dcc.h`/`dcc.cc` (`dcc_setup_new_game()`, called from
`_setup_generic` under `game_is_dcc()`, sets `you.props["dcc_version"] = 1`)
and `source/l-dcc.cc` (dlua table `dcc` with `is_active()` and
`enable_for_script()`), registered in `dlua.cc`/`l-libs.h` and listed in
`Makefile.obj`, `crawl.vcxproj` and its `.filters`. `scripts/dcc_smoke.lua` is
now the gate's fourth command: it checks the mode, a 12-floor Dungeon, and
generates D:1, D:6 (each with a down stair) and D:12. A broken check exits 1.
Discovery: `initialise_branch_depths()` already sets the Descent depth and both
Descent branch keys for every Descent-type game, so `enable_for_script()` just
sets the type and calls it; there was nothing extra to factor out.
Reviewers changed: `dcc.enable_for_script()` now raises a Lua error unless
`crawl_state.script` or `crawl_state.test` is set (`-script` sets both).

## 2026-10-25 — Phase 2a done

Landed: `source/dcc-clock.h`/`dcc-clock.cc`, a per-floor clock (1500 turns on
D:1-3, 1200 elsewhere) in `you.props`, started from `load_level()` and ticked
from `player_reacts()`, with dlua helpers and smoke checks. At zero the floor
collapses: a forced one-floor descent with 20% HP damage, or at a branch
bottom the Zot-touch max-HP penalty and a 100-turn retry clock.
User decision (Resolved decision 8): the clock runs only on connected-branch
floors and pauses in portals, the Abyss, Pan and Zigs.
Reviewers changed:
- the hook passes `just_created_level`; a newly created floor always gets a
  fresh clock (Pan, portals, Zigs and repeat Abyss trips used to reuse old
  clocks under one `describe()` key). The park table remains for real
  revisits on connected floors only: it never stores a spent clock and keeps
  at most 16 entries, so a collapsed floor cannot collapse again on return;
- the collapse uses `floor_transition()` directly instead of the shaft-trap
  path (no trap destroyed, no shaft messages, no shaft milestone), and HP
  loss, `dcc_collapsed_count` and the note apply only after the level has
  really changed, so a failed fall is penalised once;
- the Zot-touch penalty is now `zot_touch_max_hp()` in `zot.cc`, shared by
  both (a pure move, recorded in the CLAUDE.md isolation rule);
- `dcc.tick()` returns the collapse result, and the smoke checks a collapse,
  the D:12 no-floor penalty, and the pause in the Abyss and a portal;
- CLAUDE.md now allows the one `#include "dcc*.h"` a trunk hook needs.
