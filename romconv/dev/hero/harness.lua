-- MAME a2600 Lua harness for H.E.R.O. research (see run_mame.sh).
-- Script lines: "<frame> <keys>" with keys from UDLRB (joystick), S (reset),
-- G (select), "-" none, held until the next line; "P <frame> <addr> <val>"
-- pokes RAM (hex). Writes ram.bin (RAM $80-$FF per frame), reads.txt
-- ("bank addr count", bank aware via the F8 hotspots) and snap_<frame>.png.
-- env: H_SCRIPT (input script file), H_FRAMES (total), H_SNAP (comma frames),
--      H_RAM (1 = dump RAM each frame to ram.bin), H_READS (from,to frame window for ROM read log)
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen = manager.machine.screens[":screen"]
local ports = manager.machine.ioport.ports
local function field(tag, name) return ports[tag].fields[name] end
local F = {
  U = field(":joyport1:joy:JOY", "P1 Up"), D = field(":joyport1:joy:JOY", "P1 Down"),
  L = field(":joyport1:joy:JOY", "P1 Left"), R = field(":joyport1:joy:JOY", "P1 Right"),
  B = field(":joyport1:joy:JOY", "P1 Button 1"), S = field(":SWB", "Reset Game"),
  G = field(":SWB", "Select Game"),
}
-- script lines: "<frame> <keys>" keys among UDLRBSG or '-' = none; held until next line
local script = {}
for line in io.lines(os.getenv("H_SCRIPT")) do
  local f, k = line:match("^(%d+)%s+(%S+)")
  if f then table.insert(script, {tonumber(f), k}) end
end
local pokes = {} -- "P <frame> <addr hex> <val hex>"
for line in io.lines(os.getenv("H_SCRIPT")) do
  local f, a, v = line:match("^P%s+(%d+)%s+(%x+)%s+(%x+)")
  if f then table.insert(pokes, {tonumber(f), tonumber(a, 16), tonumber(v, 16)}) end
end
local total = tonumber(os.getenv("H_FRAMES") or "600")
local snaps = {}
for s in (os.getenv("H_SNAP") or ""):gmatch("%d+") do snaps[tonumber(s)] = true end
local ramf = os.getenv("H_RAM") == "1" and io.open("ram.bin", "wb") or nil
-- H_RAM_TAP=1: dump RAM when the game increments its frame counter ($81 at
-- $D700), a fixed point of its loop (frame logic done, overscan not), instead
-- of at MAME's frame end, which drifts against the game loop. Records are
-- 131 bytes: RAM $80-$FF, the last SWCHA, INPT4 and SWCHB read.
local tapf = os.getenv("H_RAM_TAP") == "1" and io.open("ram.bin", "wb") or nil
if tapf then ramf = nil end
local rfrom, rto = (os.getenv("H_READS") or "0,-1"):match("(%-?%d+),(%-?%d+)")
rfrom, rto = tonumber(rfrom), tonumber(rto)
local bank = 1
local frame = 0
local reads = {}
TAPR = mem:install_read_tap(0x1000, 0x1fff, "rom", function(off, data, mask)
  local a = off & 0xfff
  if frame >= rfrom and frame <= rto then
    local k = bank * 4096 + a
    reads[k] = (reads[k] or 0) + 1
  end
  if a == 0xff8 then bank = 0 elseif a == 0xff9 then bank = 1 end
end)
TAPW = mem:install_write_tap(0x1000, 0x1fff, "romw", function(off, data, mask)
  local a = off & 0xfff
  if a == 0xff8 then bank = 0 elseif a == 0xff9 then bank = 1 end
end)
-- each tap record: RAM $80-$FF, then the last SWCHA, INPT4 and SWCHB values the game read
local swcha, inpt4, swchb = 0xff, 0xff, 0xff
if tapf then
  TAPS = mem:install_read_tap(0x280, 0x280, "swcha", function(off, data, mask) swcha = data end)
  TAPB = mem:install_read_tap(0x282, 0x282, "swchb", function(off, data, mask) swchb = data end)
  TAPI = mem:install_read_tap(0x0c, 0x0c, "inpt4", function(off, data, mask) inpt4 = data end)
  TAPF = mem:install_write_tap(0x81, 0x81, "frame", function(off, data, mask)
    -- INC is read-modify-write: the 6502 first writes the old value back
    if data == mem:read_u8(0x81) then return end
    local t = {}
    for a = 0x80, 0xff do t[#t + 1] = string.char(a == 0x81 and data or mem:read_u8(a)) end
    t[#t + 1] = string.char(swcha)
    t[#t + 1] = string.char(inpt4)
    t[#t + 1] = string.char(swchb)
    tapf:write(table.concat(t))
  end)
end
local si = 1
local cur = "-"
SUB = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  while si <= #script and script[si][1] <= frame do cur = script[si][2]; si = si + 1 end
  for k, f in pairs(F) do
    if cur:find(k, 1, true) then f:set_value(1) else f:clear_value() end
  end
  for _, p in ipairs(pokes) do if p[1] == frame then mem:write_u8(p[2], p[3]) end end
  if ramf then
    local t = {}
    for a = 0x80, 0xff do t[#t + 1] = string.char(mem:read_u8(a)) end
    ramf:write(table.concat(t))
  end
  if snaps[frame] then screen:snapshot(string.format("snap_%05d.png", frame)) end
  if frame >= total then
    if ramf then ramf:close() end
    if tapf then tapf:close() end
    local f = io.open("reads.txt", "w")
    for k, n in pairs(reads) do f:write(string.format("%d %04x %d\n", k // 4096, k % 4096, n)) end
    f:close()
    manager.machine:exit()
  end
end)
