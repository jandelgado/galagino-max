-- Dump the main CPU's decrypted opcode and data images (0000-bfff) after
-- frame 1, to check romconv's decryption against MAME. Output dir: env OUT.
-- Env BANKS=n: images are 0000-7fff followed by banks 0..n-1 at 8000-bfff
-- (bank1/bank1d entries), romconv's main ROM + banks layout.
local out = os.getenv("OUT")
local banks = tonumber(os.getenv("BANKS") or "0")
local n = 0

local function dump(f, space, from, to)
  for a = from, to do f:write(string.char(space:read_u8(a))) end
end

local function image(space, name, bank)
  local f = io.open(out .. "/" .. name, "wb")
  if banks == 0 then
    dump(f, space, 0, 0xbfff)
  else
    dump(f, space, 0, 0x7fff)
    for e = 0, banks - 1 do
      bank.entry = e
      dump(f, space, 0x8000, 0xbfff)
    end
  end
  f:close()
end

emu.register_frame_done(function()
  n = n + 1
  if n < 2 then return end
  local cpu = manager.machine.devices[":maincpu"]
  local mb = manager.machine.memory.banks
  image(cpu.spaces["program"], "data.bin", mb[":bank1"])
  if cpu.spaces["opcodes"] then image(cpu.spaces["opcodes"], "ops.bin", mb[":bank1d"]) end
  manager.machine:exit()
end)
