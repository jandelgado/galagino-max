-- MAME docastle reference capture for dev.docastle.mame_fixture.
-- Writes ram_trace.txt: "<frame> <sprites 200> <video+color b000-b7ff>" (hex)
-- for every frame FIRST..LAST and a snapshot every STEP frames.
-- Never read a000-a7ff: in MAME 0.289 a read asserts the main CPU's WAIT.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local spr = manager.machine.memory.shares[":spriteram"]
local first = tonumber(os.getenv("FIRST") or "300")
local last = tonumber(os.getenv("LAST") or "1800")
local step = tonumber(os.getenv("STEP") or "10")
local out = io.open("ram_trace.txt", "w")
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f >= first then
    local t = {}
    for a = 0, 0x1ff do t[#t + 1] = string.format("%02x", spr:read_u8(a)) end
    t[#t + 1] = " "
    for a = 0xb000, 0xb7ff do t[#t + 1] = string.format("%02x", sp:read_u8(a)) end
    out:write(f, " ", table.concat(t), "\n")
    if f % step == 0 then manager.machine.video:snapshot() end
  end
  if f == last then out:close() manager.machine:exit() end
end)
