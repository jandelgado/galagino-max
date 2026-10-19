#ifndef TRON_DIPSWITCHES_H
#define TRON_DIPSWITCHES_H

// SSIO IP3, DIP switch bank SW1 at B3 (MAME INPUT_PORTS_START(tron)):
//   bit 0  coin meters:     0 = 2, 1 = 1
//   bit 1  cabinet:         0 = upright, 1 = cocktail
//   bit 2  allow continue:  0 = yes, 1 = no
//   bit 3-6 unused (0)
//   bit 7  cocktail fire button, active low: idle high
#define TRON_IP3 0x80

#endif
