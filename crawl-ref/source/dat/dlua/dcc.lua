------------------------------------------------------------------------------
-- dcc.lua: Crawl Live (DCC) Lua callbacks for the safe-room vaults.
--
-- Loaded by dat/des/dcc/safe_rooms.des (crawl_require), so the callbacks
-- below exist whenever a saved level's markers are read back. The dcc table
-- they call is the dlua library in l-dcc.cc.
------------------------------------------------------------------------------

-- The loot box kiosk: a function_at_spot marker on the safe room's K cell
-- calls this whenever the player steps onto it. The C++ side does the rest:
-- it refuses off a safe-room kiosk, says so if there is nothing to open,
-- asks, and opens the boxes (dcc-boxes.cc dcc_open_boxes()).
function dcc_kiosk_activate(data, triggerable, triggerer, marker, ev)
  dcc.open_boxes()
end
