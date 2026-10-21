-- MAME tokisens (System 2, ROT90, 8 VRAM pages): attract mode, no input.
-- At the frames in env FRAMES dump all 8 VRAM pages, palette RAM, sprite
-- RAM and the video mode (PPI port B), plus a snapshot each. VRAM is
-- banked into e000-efff by PPI port C bits 1-2 and not a MAME share, so
-- a write tap mirrors it. Frames whose bg page selects (page 0 740-747)
-- change mid-frame are skipped: MAME redraws partially there. Output dir:
-- env OUT.
local frames, last = {}, 0
for f in string.gmatch(os.getenv("FRAMES"), "%d+") do
  frames[tonumber(f)] = true
  last = math.max(last, tonumber(f))
end
local out = os.getenv("OUT")
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local vram, bank, mode, selDirty = {}, 0, 0, false
for i = 0, 0x3fff do vram[i] = 0 end
local n = 0

t_c = cpu.spaces["io"]:install_write_tap(0x16, 0x16, "portc", function(o, d, m)
  bank = (d >> 1) & 3
end)
t_b = cpu.spaces["io"]:install_write_tap(0x15, 0x15, "portb", function(o, d, m)
  mode = d
end)
t_v = prog:install_write_tap(0xe000, 0xefff, "vram", function(o, d, m)
  local a = bank * 0x1000 + (o - 0xe000)
  vram[a] = d
  if a >= 0x740 and a < 0x748 then selDirty = true end
end)

local function hex(t, from, len)
  local s = {}
  for i = from, from + len - 1 do s[#s + 1] = string.format("%02x", t[i]) end
  return table.concat(s)
end

local function dump(a, len)
  local t = {}
  for i = 0, len - 1 do t[#t + 1] = string.format("%02x", prog:read_u8(a + i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  n = n + 1
  if frames[n] and not selDirty then
    local f = io.open(string.format("%s/%d.txt", out, n), "w")
    f:write(hex(vram, 0, 0x4000), "\n", dump(0xd800, 0x600), "\n", dump(0xd000, 0x800), "\n",
            string.format("%02x", mode), "\n")
    f:close()
    manager.machine.video:snapshot()
  end
  selDirty = false
  if n > last then manager.machine:exit() end
end)
