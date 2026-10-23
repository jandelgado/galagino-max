-- Juno First video reference: at the FRAMES below, a snapshot plus the
-- render input MAME used for it (frame_done = after the screen update):
-- the 32 KB bitmap 0000-7fff and the palette 8000-800f. Title screen and
-- attract play (no input).
--   OUT=<dir> mame junofrst -snapshot_directory <dir> -autoboot_script ...
-- video_fixture.py turns <dir>/state.txt + snapshots into mame_frames.h.
local dir = os.getenv("OUT")
local FRAMES = {[600] = true, [1500] = true, [1800] = true}
local LAST = 1800
local out = io.open(dir .. "/state.txt", "w")
local f = 0

local function hex(space, base, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.format("%02x", space:read_u8(base + i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  f = f + 1
  if FRAMES[f] then
    local prog = manager.machine.devices[":maincpu"].spaces["program"]
    out:write(string.format("%d %s %s\n", f, hex(prog, 0x0000, 0x8000), hex(prog, 0x8000, 0x10)))
    manager.machine.video:snapshot()
  end
  if f == LAST then
    out:close()
    manager.machine:exit()
  end
end)
