-- Krull lockstep reference: at every NMI entry append SP (2 bytes LE), the
-- 8088 RAM 0000-0fff, video RAM 3800-3bff and char RAM 4000-4fff to OUT
-- (9218 bytes per NMI). NMI entry = the handler's first instruction
-- `inc word ptr [0D83h]` (0x1366, vector in RAM at 0x0008); the tap on its
-- write sees RAM before the increment. MAME reports PC 0x1369 there.
-- Inputs change at fixed NMI indices, so the host test replays them exactly.
--   OUT=<file> SCEN=attract|game1 NMIS=<n> mame krull -autoboot_script ...
-- ram_fixture.py turns the dump into CRCs (source/test/test_krull_boot).
local out = io.open(os.getenv("OUT"), "wb")
local scen = os.getenv("SCEN") or "attract"
local nmis = tonumber(os.getenv("NMIS") or "600")
local NMI_PC = 0x1369
local NMI_COUNTER = 0x0d83

-- scenario -> { [nmi index] = { field, value } ... }, mirrored in the host test
local SCRIPTS = {
  attract = {},
  game1 = { [600] = { "Coin 1", 1 }, [605] = { "Coin 1", 0 }, [660] = { "1 Player Start", 1 },
            [665] = { "1 Player Start", 0 } },
}

local nmi = 0
local tap = nil
local cpu = nil

local function on_nmi()
  local step = SCRIPTS[scen][nmi]
  if step then
    manager.machine.ioport.ports[":IN1"].fields[step[1]]:set_value(step[2])
  end
  local prog = cpu.spaces["program"]
  out:write(string.pack("<I2", cpu.state["SP"].value))
  out:write(prog:read_range(0x0000, 0x0fff, 8))
  out:write(prog:read_range(0x3800, 0x3bff, 8))
  out:write(prog:read_range(0x4000, 0x4fff, 8))
  nmi = nmi + 1
  if nmi == nmis then
    out:close()
    manager.machine:exit()
  end
end

emu.register_frame_done(function()
  if tap then return end
  cpu = manager.machine.devices[":maincpu"]
  tap = cpu.spaces["program"]:install_write_tap(NMI_COUNTER, NMI_COUNTER, "nmi", function(offset, data, mask)
    local ok, err = pcall(function()
      local pc = cpu.state["PC"].value
      if pc == NMI_PC then on_nmi() end
    end)
    if not ok then print(err) end
  end)
end)
