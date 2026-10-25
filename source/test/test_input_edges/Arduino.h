#pragma once
// host stub: machineBase.h includes Arduino.h
#include <functional>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define INPUT_PULLUP 0
void pinMode(int, int);
int digitalRead(int);
unsigned long millis();
