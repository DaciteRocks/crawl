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
