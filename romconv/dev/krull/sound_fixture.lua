-- Krull sound board reference: every sound command the main CPU writes
-- (0x5802) and every write of the sound 6502 to the DAC (1000, mirrored at
-- 9000: A15 not decoded), stamped in sound CPU cycles (894886 Hz since
-- power on). Attract mode, FRAMES frames.
--   OUT=<file> FRAMES=<n> mame krull -autoboot_script ...
-- Lines: "S <cycle>" (taps installed: nothing before is recorded, taps only
-- work installed from frame_done) | "C <cycle> <cmd>" | "D <cycle> <value>"
local out = io.open(os.getenv("OUT"), "w")
local frames = tonumber(os.getenv("FRAMES") or "1400")
local HZ = 894886
local f, taps = 0, {}

local function now()
  return math.floor(manager.machine.time:as_double() * HZ + 0.5)
end

emu.register_frame_done(function()
  f = f + 1
  if #taps == 0 then
    out:write(string.format("S %d 00\n", now()))
    local main = manager.machine.devices[":maincpu"].spaces["program"]
    local snd = manager.machine.devices[":r1sound:audiocpu"].spaces["program"]
    taps[1] = main:install_write_tap(0x5800, 0x5fff, "cmd", function(o, d, m)
      if (o & 7) == 2 then out:write(string.format("C %d %02x\n", now(), d)) end
    end)
    taps[2] = snd:install_write_tap(0x1000, 0x1fff, "dac", function(o, d, m)
      out:write(string.format("D %d %02x\n", now(), d))
    end)
  end
  if f == frames then
    out:close()
    manager.machine:exit()
  end
end)
