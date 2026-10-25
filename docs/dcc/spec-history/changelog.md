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
