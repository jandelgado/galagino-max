#ifndef SOLOMON_DIPSWITCHES_H
#define SOLOMON_DIPSWITCHES_H

// DSW1 (read e604), MAME INPUT_PORTS_START( solomon ):
// b0    demo sounds: 0 = on, set at runtime from input->demoSoundsOff()
// b1    cabinet: 1 = upright
// b2-3  lives: 00 = 3, 11 = 2, 10 = 4, 01 = 5
// b4-5  coin B: 00 = 1C1C
// b6-7  coin A: 00 = 1C1C
#define SOLOMON_DSW1 0x02
#define SOLOMON_DSW1_DEMO_SOUND_OFF 0x01

// DSW2 (read e605):
// b0-1  difficulty: 00 = normal, 10 = easy, 01 = harder, 11 = difficult
// b2-3  timer speed: 00 = normal, 10 = slow, 01 = faster, 11 = fastest
// b4    extra: 0 = normal
// b5-7  bonus life: 000 = 30k 200k 500k
#define SOLOMON_DSW2 0x00

#endif
