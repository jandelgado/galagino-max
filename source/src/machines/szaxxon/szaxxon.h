#ifndef SZAXXON_H
#define SZAXXON_H

#include "../zaxxon/zaxxon.h"
#include "szaxxon_arena_plan.h"

// Super Zaxxon (Sega 1982): zaxxon board, all ROMs its own, main CPU
// encrypted (315-5013). Samples are zaxxon's (MAME sampleof="zaxxon").
class szaxxon : public zaxxon {
public:
  szaxxon();

  signed char machineType() override { return MCH_SZAXXON; }
};

#endif
