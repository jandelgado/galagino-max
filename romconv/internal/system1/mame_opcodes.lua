-- Dump the main CPU's decrypted opcode and data images (0000-bfff) after
-- frame 1, to check romconv's decryption against MAME. Output dir: env OUT.
local out = os.getenv("OUT")
local n = 0

local function dump(space, name)
  local f = io.open(out .. "/" .. name, "wb")
  for a = 0, 0xbfff do f:write(string.char(space:read_u8(a))) end
  f:close()
end

emu.register_frame_done(function()
  n = n + 1
  if n < 2 then return end
  local cpu = manager.machine.devices[":maincpu"]
  dump(cpu.spaces["program"], "data.bin")
  if cpu.spaces["opcodes"] then dump(cpu.spaces["opcodes"], "ops.bin") end
  manager.machine:exit()
end)
