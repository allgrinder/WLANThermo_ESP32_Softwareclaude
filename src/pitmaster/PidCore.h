/***************************************************
    Copyright (C) 2016  Steffen Ochs
    Copyright (C) 2019  Martin Koerner
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

    HISTORY: Please refer Github History

****************************************************/
#pragma once

#include <stdint.h>

// PID core of the pitmaster, moved out of Pitmaster::pidCalc() without
// changing the arithmetic. It has no Arduino dependency so that the very
// same code runs in the host simulator (test/sim).
//
// Controller form (all times in seconds):
//   e      = w - x                      [°C]
//   p_out  = kp * e                     kp [%/°C]
//   i_out  = ki * esum, esum += e * dt  ki [%/(°C*s)]  (anti windup 0..95 %)
//   d_out  = kd * edif                  kd [%*s/°C], edif = slope of e over
//                                       dCount cycles in °C/s
class PidCore
{
public:
  PidCore();
  void reset();
  void setDCount(uint8_t dCount);
  uint8_t getDCount() { return this->dCount; }

  // x: filtered actual temperature, w: target temperature
  // highResolution: error resolution 0.01 (SSR) instead of 0.1
  // jumpThreshold: temperature below which the 25 degree jump range is used
  // dtMs: controller cycle time in ms
  // jumpEnabled: false disables the jump detection (e.g. during autotune)
  float calc(float x, float w, float kp, float ki, float kd, bool highResolution,
             float jumpThreshold, float dtMs, bool jumpEnabled = true);

  float getP() { return this->pOut; }
  float getI() { return this->iOut; }
  float getD() { return this->dOut; }
  float getEsum() { return this->esum; }
  bool isJump() { return this->jump; }
  void clearJump() { this->jump = false; this->ampch = 0u; }
  // bumpless start: preload the integral part with the given output in %
  void preloadIntegral(float iOut, float ki);

private:
  float esum;   // PITMASTER I-PART DIFFERENZ SUM
  float elast;  // PITMASTER D-PART DIFFERENZ LAST
  float Ki_alt; // PITMASTER I-PART CACHE
  uint8_t ecount;
  uint8_t dCount;
  float edif;
  bool jump;
  uint8_t ampch; // Amplitudenwechsel
  float pOut;
  float iOut;
  float dOut;
};
