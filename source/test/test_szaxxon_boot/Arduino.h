#pragma once
// host stub: machineBase.h includes Arduino.h
#include <functional>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// Arduino random(max), used by zaxxon::gameLeds
inline long random(long max) { return rand() % max; }
