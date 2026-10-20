#ifndef TAPPER_DIPSWITCHES_H
#define TAPPER_DIPSWITCHES_H

// MAME INPUT_PORTS_START(tapper), SSIO IP3, DIP bank at B3:
// bits 0-1 unused (1)
// bit 2 demo sounds, 0 = on
// bits 3-5 unused (1)
// bit 6 cabinet, 1 = upright
// bit 7 coin meters, 1 = one meter
#define TAPPER_IP3 0xfb
#define TAPPER_IP3_DEMO_OFF 0x04

#endif
