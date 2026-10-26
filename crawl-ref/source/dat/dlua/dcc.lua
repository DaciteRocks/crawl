------------------------------------------------------------------------------
-- dcc.lua: Crawl Live (DCC) Lua callbacks for the safe-room vaults.
--
-- Loaded by dat/des/dcc/safe_rooms.des (crawl_require), so the callbacks
-- below exist whenever a saved level's markers are read back. The dcc table
-- they call is the dlua library in l-dcc.cc.
------------------------------------------------------------------------------

-- The loot box kiosk: a function_at_spot marker on the safe room's K cell
-- calls this whenever the player moves onto it, by any means (the marker
-- fires on every player move). The C++ side does the rest
-- (dcc-boxes.cc dcc_kiosk_stepped()): only a deliberate step opens boxes,
-- asking first; travel, autoexplore, blinks and teleports pass silently.
function dcc_kiosk_activate(data, triggerable, triggerer, marker, ev)
  dcc.kiosk_stepped()
end
