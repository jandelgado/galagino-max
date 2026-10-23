-- MAME contra reference capture for dev.contra.mame_fixture.
-- Writes ram_trace.txt: "<frame> <ctrl #0 0-7 + #1 0-7> <written mask> <ram 0000-5fff>"
-- (hex) for frames F with F % STEP in {0, STEP - 1} between FIRST and LAST,
-- and a snapshot every STEP frames. The K007121 control registers are
-- write-only: a write tap from frame 1 tracks them (taps installed at script
-- load never fire).
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local first = tonumber(os.getenv("FIRST") or "500")
local last = tonumber(os.getenv("LAST") or "3300")
local step = tonumber(os.getenv("STEP") or "10")
local out = io.open("ram_trace.txt", "w")
local ctrl, written = {}, 0
for i = 0, 15 do ctrl[i] = 0 end
local taps = {}
local f = 0

local function track(base, idx)
  return function(offset, data, mask)
    local r = offset - base
    ctrl[idx + r] = data & 0xff
    written = written | (1 << (idx + r))
  end
end

emu.register_frame_done(function()
  f = f + 1
  if f == 1 then
    taps[1] = sp:install_write_tap(0x0000, 0x0007, "ctrl0", track(0x0000, 0))
    taps[2] = sp:install_write_tap(0x0060, 0x0067, "ctrl1", track(0x0060, 8))
  end
  local m = f % step
  if f >= first and (m == 0 or m == step - 1) then
    local t = {}
    for i = 0, 15 do t[#t + 1] = string.format("%02x", ctrl[i]) end
    t[#t + 1] = string.format(" %04x ", written)
    for a = 0, 0x5fff do t[#t + 1] = string.format("%02x", sp:read_u8(a)) end
    out:write(f, " ", table.concat(t), "\n")
    if m == 0 then manager.machine.video:snapshot() end
  end
  if f == last then out:close() manager.machine:exit() end
end)
