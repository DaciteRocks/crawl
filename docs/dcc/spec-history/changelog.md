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
