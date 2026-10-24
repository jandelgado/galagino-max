-- Tylz lockstep reference: at every NMI append SP (2 bytes LE), the 8088 RAM
-- 0000-2fff and video RAM 3800-3bff to OUT (13314 bytes per NMI). The NMI
-- handler (0x8746, vector in RAM at 0x0008) pushes 7 registers, then
-- `mov byte ptr [0C11h],0FFh` (0x8753): the tap on that write sees RAM
-- before it. MAME reports PC NMI_PC there (PROBE=1 prints it).
-- Inputs change at fixed NMI indices, so the host test replays them exactly.
--   OUT=<file> SCEN=attract|game1 NMIS=<n> mame tylz -autoboot_script ...
-- ram_fixture.py turns the dump into CRCs (source/test/test_tylz_boot).
local out = io.open(os.getenv("OUT"), "wb")
local scen = os.getenv("SCEN") or "attract"
local nmis = tonumber(os.getenv("NMIS") or "600")
local NMI_PC = tonumber(os.getenv("NMI_PC") or "0x8757")
local FRAME_FLAG = 0x0c11

-- scenario -> { [nmi index] = { port, field, value } ... }, mirrored in the host test
local SCRIPTS = {
  attract = {},
  game1 = { [600] = { ":IN1", "Coin 1", 1 }, [605] = { ":IN1", "Coin 1", 0 },
            [660] = { ":IN4", "1 Player Start", 1 }, [665] = { ":IN4", "1 Player Start", 0 } },
}

local nmi = 0
local tap = nil
local cpu = nil

local function on_nmi()
  local step = SCRIPTS[scen][nmi]
  if step then
    manager.machine.ioport.ports[step[1]].fields[step[2]]:set_value(step[3])
  end
  local prog = cpu.spaces["program"]
  out:write(string.pack("<I2", cpu.state["SP"].value))
  out:write(prog:read_range(0x0000, 0x2fff, 8))
  out:write(prog:read_range(0x3800, 0x3bff, 8))
  nmi = nmi + 1
  if nmi == nmis then
    out:close()
    manager.machine:exit()
  end
end

emu.register_frame_done(function()
  if tap then return end
  cpu = manager.machine.devices[":maincpu"]
  tap = cpu.spaces["program"]:install_write_tap(FRAME_FLAG, FRAME_FLAG, "nmi", function(offset, data, mask)
    local ok, err = pcall(function()
      local pc = cpu.state["PC"].value
      if os.getenv("PROBE") then
        print(string.format("tap pc %05x data %02x sp %04x", pc, data, cpu.state["SP"].value))
        io.stdout:flush()
      end
      if pc == NMI_PC then on_nmi() end
    end)
    if not ok then print(err) end
  end)
end)
