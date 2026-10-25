-- Crawl Live (DCC) smoke script: the headless per-phase gate.
-- Run from crawl-ref/source:  ./crawl.exe -script dcc_smoke.lua
-- Every check error()s on failure, so a failing check exits non-zero.
-- Later phases append their checks here.

local function check(cond, what)
  if not cond then
    error("dcc_smoke FAILED: " .. what)
  end
  print("dcc_smoke ok: " .. what)
end

dcc.enable_for_script()
check(dcc.is_active(), "dcc.is_active() after enable_for_script")
check(dgn.is_descent(), "dgn.is_descent() is true in DCC")
check(dgn.br_depth("D") == 12, "the Dungeon is 12 floors deep")

-- D:12 is the bottom of the Dungeon in Descent; it may lack a plain down
-- stair, so for it only successful generation is checked.
local places = {
  { place = "D:1",  need_down = true },
  { place = "D:6",  need_down = true },
  { place = "D:12", need_down = false },
}

for _, p in ipairs(places) do
  debug.goto_place(p.place)
  local ok, err = pcall(test.regenerate_level)
  check(ok, p.place .. " generates" .. (ok and "" or (": " .. tostring(err))))
  if p.need_down then
    check(test.level_has_down_stair(), p.place .. " has a down stair")
  end
end

-- Phase 2a: the per-floor clock.
debug.goto_place("D:1")
test.regenerate_level()
dcc.start_floor_clock()
check(dcc.floor_turns_left() == 1500, "D:1 floor clock starts at 1500 turns")
dcc.tick(10)
check(dcc.floor_turns_left() == 1499, "a 10-aut tick leaves 1499 turns")
dcc.tick(5)
check(dcc.floor_turns_left() == 1499, "partial turns round up (1498.5 -> 1499)")

-- Arriving on a new floor starts its clock; returning to a floor left with
-- time remaining (the Descent stair peek) resumes it instead of restarting.
debug.goto_place("D:4")
dcc.enter_floor()
check(dcc.floor_turns_left() == 1200, "D:4 floor clock starts at 1200 turns")
dcc.tick(100)
debug.goto_place("D:1")
dcc.enter_floor()
check(dcc.floor_turns_left() == 1499, "back on D:1 its clock resumes at 1499")
dcc.enter_floor()
check(dcc.floor_turns_left() == 1499, "re-entering the same floor changes nothing")
debug.goto_place("D:4")
dcc.enter_floor()
check(dcc.floor_turns_left() == 1190, "back on D:4 its clock resumes at 1190")

-- Reaching zero calls the collapse, which is skipped in script mode (there is
-- no real player to drop between levels); it must not crash.
dcc.set_floor_turns_left(0)
dcc.tick(10)
check(dcc.floor_turns_left() == 0, "the clock stops at 0 and collapse does not crash")
dcc.set_floor_turns_left(3)
check(dcc.floor_turns_left() == 3, "set_floor_turns_left sets the clock")
