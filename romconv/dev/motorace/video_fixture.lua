-- MotoRace video reference: every STEP frames from FIRST to LAST, a snapshot
-- plus the render input MAME used for it (frame_done = after the screen
-- update, before the vblank IRQ handler runs): video RAM 8000-8fff, sprite
-- RAM c800-c9ff, the last 9000/a000 scroll writes and the player's lateral
-- position (e303). Coin at 600, start at 660, then accelerate held so play
-- reaches later course sections, steering left/right so the player crosses
-- the road.
--   OUT=<dir> mame motorace -snapshot_directory <dir>/snap -autoboot_script ...
-- video_fixture.py turns <dir>/state.txt + snapshots into mame_frames.h.
local dir = os.getenv("OUT")
local FIRST, STEP, LAST = 100, 20, 6000
local out = io.open(dir .. "/state.txt", "w")
local f = 0
local scroll = {0, 0}
local taps = nil

local function hex(share, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.format("%02x", share:read_u8(i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  f = f + 1
  if not taps then
    local prog = manager.machine.devices[":maincpu"].spaces["program"]
    taps = {
      prog:install_write_tap(0x9000, 0x9000, "sl", function(offset, data, mask) scroll[1] = data end),
      prog:install_write_tap(0xa000, 0xa000, "sh", function(offset, data, mask) scroll[2] = data end),
    }
  end
  local ports = manager.machine.ioport.ports
  if f == 600 then ports[":SYSTEM"].fields["Coin 1"]:set_value(1) end
  if f == 605 then ports[":SYSTEM"].fields["Coin 1"]:set_value(0) end
  if f == 660 then ports[":SYSTEM"].fields["1 Player Start"]:set_value(1) end
  if f == 665 then ports[":SYSTEM"].fields["1 Player Start"]:set_value(0) end
  if f == 900 then ports[":P1"].fields["P1 Button 1"]:set_value(1) end
  if f > 900 then
    local phase = math.floor(f / 90) % 4
    ports[":P1"].fields["P1 Left"]:set_value(phase == 1 and 1 or 0)
    ports[":P1"].fields["P1 Right"]:set_value(phase == 3 and 1 or 0)
  end
  if f >= FIRST and (f - FIRST) % STEP == 0 then
    local sh = manager.machine.memory.shares
    local prog = manager.machine.devices[":maincpu"].spaces["program"]
    out:write(string.format("%d %02x%02x %02x %s %s\n", f, scroll[1], scroll[2], prog:read_u8(0xe303),
      hex(sh[":videoram"], 0x1000), hex(sh[":spriteram"], 0x200)))
    manager.machine.video:snapshot()
  end
  if f == LAST then
    out:close()
    manager.machine:exit()
  end
end)
