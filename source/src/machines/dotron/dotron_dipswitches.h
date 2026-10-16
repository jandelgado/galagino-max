#ifndef DOTRON_DIPSWITCHES_H
#define DOTRON_DIPSWITCHES_H

// MAME INPUT_PORTS_START(dotron)
// SSIO IP2 bit 7: cabinet DIP, 1 = upright
#define DOTRON_IP2_UPRIGHT 0x80
// SSIO IP3, DIP bank at B3: bit 0 coin meters (1 = one meter), bits 1-7 unused (1)
#define DOTRON_IP3 0xff

#endif
