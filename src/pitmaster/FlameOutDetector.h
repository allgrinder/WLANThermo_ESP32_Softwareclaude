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

// Flame out detection for gas burners without flame failure device.
// Pure C++ (no Arduino dependency), also used by the host simulator.
//
// Trigger condition (all must be true):
//  - the temperature dropped by more than `drop` °C within `windowS`
//    and the drop is steady (no rise > 0.5 °C between 10 s samples since the
//    maximum of the window)
//  - the temperature is below the setpoint
//  - the output is >= reference for at least 60 s; reference is the bias
//    (learned from autotune or from steady state operation) or 50 % if no
//    bias is known yet
//  - no open lid detected now or within the last window
// The trigger is latched until acknowledge() is called.

#define FO_SAMPLE_INTERVAL_S 10u
#define FO_MAX_SAMPLES 61u // up to 10 min window
#define FO_DEFAULT_DROP 15.0f
#define FO_DEFAULT_WINDOW_S 300u
#define FO_DEFAULT_OUTPUT_REF 50.0f
#define FO_RISE_TOLERANCE 0.5f
#define FO_OUTPUT_HOLD_S 60u
#define FO_BIAS_BAND 3.0f
#define FO_BIAS_LEARN_S 300u

class FlameOutDetector
{
public:
  FlameOutDetector();
  void configure(float drop, uint16_t windowS);
  // reset history (keeps a known bias)
  void reset();
  void setBias(float bias);
  float getBias() const { return bias; }
  bool isBiasKnown() const { return biasKnown; }

  // call once per controller cycle (1 s)
  // returns true when the flame out is detected (latched)
  bool update(float x, float setpoint, float output, bool lidOpen, uint32_t nowMs);

  bool isTriggered() const { return triggered; }
  // partial condition (steady drop below setpoint seen), for telemetry
  bool isSuspected() const { return suspected; }
  void acknowledge();

private:
  void addSample(float x);

  float drop;
  uint16_t windowS;
  float samples[FO_MAX_SAMPLES];
  uint8_t sampleCount;
  uint8_t sampleHead;
  uint32_t lastSampleMs;
  bool sampleStarted;
  uint32_t lidHoldUntilMs;
  bool lidHold;
  uint32_t outputHighSinceMs;
  bool outputHigh;
  float bias;
  bool biasKnown;
  uint32_t biasSamples;
  bool triggered;
  bool suspected;
};
