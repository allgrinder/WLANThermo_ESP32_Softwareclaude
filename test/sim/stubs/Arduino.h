/***************************************************
    Copyright (C) 2026  WLANThermo gas fork contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************/
// Minimal Arduino/ESP32 stub for the host simulator. Only what the
// pitmaster code uses. Time and the LEDC PWM are simulated.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <algorithm>

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int uint;

#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

using std::max;
using std::min;

class String : public std::string
{
public:
  String() : std::string() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
};

// ---- simulated time ----
extern uint32_t gSimMillis;
inline uint32_t millis() { return gSimMillis; }
inline void delay(uint32_t ms) { gSimMillis += ms; }

// ---- simulated IO ----
#define SIM_MAX_CHANNELS 16
#define SIM_MAX_PINS 40
extern uint32_t gSimLedcDuty[SIM_MAX_CHANNELS];
extern int gSimPinChannel[SIM_MAX_PINS]; // -1: not attached
extern uint8_t gSimDac[SIM_MAX_PINS];
extern uint8_t gSimDigital[SIM_MAX_PINS];

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t val) { gSimDigital[pin] = val; }
inline void dacWrite(uint8_t pin, uint8_t val) { gSimDac[pin] = val; }
inline double ledcSetup(uint8_t, double freq, uint8_t) { return freq; }
inline void ledcAttachPin(uint8_t pin, uint8_t channel) { gSimPinChannel[pin] = channel; }
inline void ledcDetachPin(uint8_t pin) { gSimPinChannel[pin] = -1; }
inline void ledcWrite(uint8_t channel, uint32_t duty) { gSimLedcDuty[channel] = duty; }
inline uint32_t ledcRead(uint8_t channel) { return gSimLedcDuty[channel]; }

// Arduino map() works on long, float arguments are truncated
inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

class SimSerial
{
public:
  void println(const char *) {}
  void print(const char *) {}
  template <typename... Args>
  void printf(const char *, Args...) {}
};
extern SimSerial Serial;
