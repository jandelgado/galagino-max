-- Tylz video reference: every STEP frames from FIRST to LAST, a snapshot
-- plus the render input MAME used for it (frame_done = after the screen
-- update, before the vblank NMI handler runs): video RAM 3800-3bff, sprite
-- RAM 3000-30ff, palette 5000-501f and the last 0x5803 write (b0 bg over
-- sprites). Coin at 600, start at 660 (no stick input) so frames show play.
--   OUT=<dir> mame tylz -snapshot_directory <dir>/snap -autoboot_script ...
-- video_fixture.py turns <dir>/state.txt + snapshots into mame_frames.h.
local dir = os.getenv("OUT")
local FIRST, STEP, LAST = 100, 20, 3000
local out = io.open(dir .. "/state.txt", "w")
local f = 0
local ctrl = 0
local tap = nil

local function hex(share, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.format("%02x", share:read_u8(i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  f = f + 1
  if not tap then
    local prog = manager.machine.devices[":maincpu"].spaces["program"]
    tap = prog:install_write_tap(0x5803, 0x5803, "ctrl", function(offset, data, mask) ctrl = data end)
  end
  local ports = manager.machine.ioport.ports
  if f == 600 then ports[":IN1"].fields["Coin 1"]:set_value(1) end
  if f == 605 then ports[":IN1"].fields["Coin 1"]:set_value(0) end
  if f == 660 then ports[":IN4"].fields["1 Player Start"]:set_value(1) end
  if f == 665 then ports[":IN4"].fields["1 Player Start"]:set_value(0) end
  if f >= FIRST and (f - FIRST) % STEP == 0 then
    local sh = manager.machine.memory.shares
    out:write(string.format("%d %02x %s %s %s\n", f, ctrl, hex(sh[":videoram"], 0x400),
      hex(sh[":spriteram"], 0x100), hex(sh[":paletteram"], 0x20)))
    manager.machine.video:snapshot()
  end
  if f == LAST then
    out:close()
    manager.machine:exit()
  end
end)
