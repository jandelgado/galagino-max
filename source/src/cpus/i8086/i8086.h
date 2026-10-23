// 8086/8088 interpreter, from FabGL (src/emudevs/i8086.h, commit fd6f1e35,
// MIT, based on 8086tiny: see LICENSE-8086tiny). One CPU per program: all
// state is static.

#pragma once

#include <stdint.h>


#ifndef I80186MODE
  #define I80186MODE 0
#endif


class i8086 {

public:

  typedef uint8_t (*Read8)(void * context, uint32_t address);
  typedef void (*Write8)(void * context, uint32_t address, uint8_t value);

  // Linear address space as the core sees it: address = (16 * seg + off) & addressMask.
  // ram/rom windows are served inline, everything else through read/write.
  // Code must run from the rom window; rom covers romBase .. addressMask.
  struct Bus {
    void *          context;
    Read8           read;
    Write8          write;        // also gets writes into the rom window
    uint8_t *       ram;          // linear 0 .. ramSize - 1
    uint32_t        ramSize;
    const uint8_t * rom;          // linear romBase .. addressMask
    uint32_t        romBase;
    uint32_t        addressMask;  // 0xfffff for a full 8088, 0xffff on Gottlieb boards
  };

  static void setBus(const Bus & bus)                     { s_bus = bus; }

  // CS:IP = FFFF:0000, flags 0, fault cleared
  static void reset();

  static void setAL(uint8_t value);
  static void setAH(uint8_t value);
  static void setBL(uint8_t value);
  static void setBH(uint8_t value);
  static void setCL(uint8_t value);
  static void setCH(uint8_t value);
  static void setDL(uint8_t value);
  static void setDH(uint8_t value);

  static uint8_t AL();
  static uint8_t AH();
  static uint8_t BL();
  static uint8_t BH();
  static uint8_t CL();
  static uint8_t CH();
  static uint8_t DL();
  static uint8_t DH();

  static void setAX(uint16_t value);
  static void setBX(uint16_t value);
  static void setCX(uint16_t value);
  static void setDX(uint16_t value);
  static void setDI(uint16_t value);
  static void setCS(uint16_t value);
  static void setDS(uint16_t value);
  static void setSS(uint16_t value);
  static void setES(uint16_t value);
  static void setIP(uint16_t value);
  static void setSP(uint16_t value);

  static uint16_t AX();
  static uint16_t BX();
  static uint16_t CX();
  static uint16_t DX();
  static uint16_t BP();
  static uint16_t SI();
  static uint16_t DI();
  static uint16_t SP();

  static uint16_t CS();
  static uint16_t ES();
  static uint16_t DS();
  static uint16_t SS();

  static bool flagIF();
  static bool flagTF();
  static bool flagCF();
  static bool flagZF();
  static bool flagOF();
  static bool flagDF();
  static bool flagSF();
  static bool flagAF();
  static bool flagPF();

  static void setFlagZF(bool value);
  static void setFlagCF(bool value);

  static uint16_t IP();

  static bool halted()                                    { return s_halted; }

  // set by a code fetch outside the rom window or an unsupported opcode,
  // sticky until reset()
  static bool fault()                                     { return s_fault; }

  static bool IRQ(uint8_t interrupt_num);

  // int 2: ignores IF, leaves HLT
  static void nmi();

  // one instruction, including its prefixes and the whole rep
  static void step();


private:

  static uint8_t WMEM8(int addr, uint8_t value);
  static uint16_t WMEM16(int addr, uint16_t value);
  static uint8_t RMEM8(int addr);
  static uint16_t RMEM16(int addr);

  static uint16_t make_flags();
  static void set_flags(int new_flags);

  static void pc_interrupt(uint8_t interrupt_num);

  static uint8_t raiseDivideByZeroInterrupt();

  static void stepEx(uint8_t const * opcode_stream);


  static Bus                s_bus;

  static bool               s_pendingIRQ;
  static uint8_t            s_pendingIRQIndex;
  static bool               s_halted;
  static bool               s_fault;

};
