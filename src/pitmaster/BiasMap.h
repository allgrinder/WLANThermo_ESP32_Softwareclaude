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

    HISTORY: Please refer Github History

****************************************************/
#pragma once

#include <stdint.h>

// Learned steady state output per temperature ("how far must the valve be
// open to hold 130 °C"). It is the inverse of the valve/smoker curve and
// is learned from steady operation, seeded by the autotune. Used as
// feedforward (integral preload) at start, setpoint change and after an
// open lid, so the PID only has to correct the rest.
// Pure C++ (no Arduino dependency), also used by the host simulator.

#define BIASMAP_T_MIN 60
#define BIASMAP_T_STEP 10
#define BIASMAP_BINS 19 // 60 .. 240 °C
#define BIASMAP_KNOWN_S 120u     // learned seconds until a bin counts as known
#define BIASMAP_LEARN_TAU_S 600.0f
#define BIASMAP_AMBIENT 20.0f
#define BIASMAP_EXTRAPOLATE_MAX 30.0f

class BiasMap
{
public:
  BiasMap();
  void clear();
  uint8_t getProfileId() const { return profileId; }
  void setProfileId(uint8_t id) { profileId = id; }

  // one steady second at setpoint `temp` with mean output `output`
  // returns true when a bin just became known (worth saving)
  bool learn(float temp, float output);
  // strong seed, e.g. from the autotune
  void seed(float temp, float output);
  // estimated steady output for `temp`, false if nothing is known
  bool estimate(float temp, float &output) const;

  bool isKnown(uint8_t bin) const { return learned[bin] >= BIASMAP_KNOWN_S; }
  float getValue(uint8_t bin) const { return value[bin]; }
  uint16_t getLearned(uint8_t bin) const { return learned[bin]; }
  void setBin(uint8_t bin, float output, uint16_t learnedS);
  uint8_t knownCount() const;

private:
  float value[BIASMAP_BINS];
  uint16_t learned[BIASMAP_BINS];
  uint8_t profileId;
};
