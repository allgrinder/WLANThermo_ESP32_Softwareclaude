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

// Relay feedback autotune (Astroem-Haegglund) with hysteresis.
// Pure C++ (no Arduino dependency), used by Pitmaster and by the host
// simulator in test/sim.

#define AT_MAX_CYCLES 8u

enum class AtState : uint8_t
{
  Idle = 0,
  Running = 1,
  Done = 2,
  Aborted = 3
};

enum class AtPhase : uint8_t
{
  None = 0,
  Approach = 1, // reach setpoint with current PID, learn bias
  Relay = 2,    // relay oscillation around the bias
  Finished = 3
};

enum class AtError : uint8_t
{
  None = 0,
  OverTemp = 1,       // x > setpoint + overTemp
  Timeout = 2,        // overall time limit reached
  SensorLost = 3,     // sensor inactive
  UserStop = 4,       // stopped by user / pitmaster type changed
  ProfileChange = 5,  // profile of the pitmaster changed
  ChannelChange = 6,  // temperature channel changed
  SetpointChange = 7, // setpoint changed
  Restart = 8,        // device restarted during autotune
  NoOscillation = 9,  // relay did not switch within halfCycleTimeout
  Disabled = 10,      // pitmaster disabled (e.g. no USB supply)
  FlameOut = 11       // flame out detected during autotune
};

enum class AtRule : uint8_t
{
  TyreusLuybenPid = 0,
  ZieglerNicholsNoOvershootPid = 1,
  TyreusLuybenPi = 2
};

// warning flags
#define AT_WARN_BIAS_FALLBACK 0x01u // setpoint not stable within approachTimeout, bias = 50 %
#define AT_WARN_NOT_CONVERGED 0x02u // maxCycles reached without convergence, mean of last 3 used

typedef struct
{
  float setpoint;             // °C
  float d;                    // relay amplitude in % (default 20)
  float h;                    // hysteresis in °C (default 1.5)
  float minOutput;            // lower output limit in % (default 0)
  float maxOutput;            // upper output limit in % (default 100)
  AtRule rule;                // tuning rule
  float overTemp;             // abort when x > setpoint + overTemp (default 25)
  uint32_t timeoutS;          // overall timeout in s (default 90 min)
  float stableBand;           // +-band for the approach phase (default 3)
  uint32_t stableS;           // time within band for bias (default 5 min)
  uint32_t approachTimeoutS;  // bias fallback after (default 30 min)
  uint8_t minCycles;          // (default 4)
  uint8_t maxCycles;          // (default 8, max AT_MAX_CYCLES)
  float tolerance;            // relative spread of the last 3 cycles (default 0.15)
  uint32_t halfCycleTimeoutS; // max time without relay switch (default 20 min)
} AtConfig;

class AutoTuneRelay
{
public:
  AutoTuneRelay();
  static void defaultConfig(AtConfig &config);

  void start(const AtConfig &config, uint32_t nowMs);
  // x: filtered temperature, sensorActive: false if the sensor is lost,
  // pidOutput: output of the regular PID (used in the approach phase).
  // Returns the output (0..100 %) to apply.
  float update(float x, bool sensorActive, float pidOutput, uint32_t nowMs);
  void abort(AtError error);
  // mark a run that was interrupted by a restart (status only)
  void markRestartAbort(float setpoint);

  bool isRunning() const { return AtState::Running == state; }
  bool needsPid() const { return isRunning() && (AtPhase::Approach == phase); }
  AtState getState() const { return state; }
  AtPhase getPhase() const { return phase; }
  AtError getError() const { return error; }
  uint8_t getWarnings() const { return warnings; }
  uint8_t getCycle() const { return cycleCount; }
  uint8_t getMaxCycles() const { return config.maxCycles; }
  float getSetpoint() const { return config.setpoint; }
  float getBias() const { return bias; }
  // estimate of the steady state output at the setpoint (used for a
  // bumpless PID start): min(bias, mean output of the last 3 relay cycles)
  float getOutputMean() const { return outputMean; }
  float getTu() const { return tu; }       // s
  float getA() const { return a; }         // °C
  float getKu() const { return ku; }       // %/°C
  float getKp() const { return kp; }
  float getKi() const { return ki; }
  float getKd() const { return kd; }
  float getOutput() const { return output; }
  float getMinOutput() const { return config.minOutput; }
  uint32_t getElapsedS(uint32_t nowMs) const { return (nowMs - startMs) / 1000u; }

  // tuning rules, see AutoTuneRelay.cpp for the unit derivation
  static void computeGains(AtRule rule, float ku, float tuS, float &kp, float &ki, float &kd);

private:
  void finishApproach(float newBias, uint32_t nowMs);
  void onRisingSwitch(uint32_t nowMs);
  bool lastCyclesConverged() const;
  void computeResult();
  float clampOutput(float value) const;

  AtConfig config;
  AtState state;
  AtPhase phase;
  AtError error;
  uint8_t warnings;
  uint32_t startMs;

  // approach
  bool inBand;
  uint32_t bandStartMs;
  float outputSum;
  uint32_t outputCount;

  // relay
  bool relayHigh;
  uint32_t lastSwitchMs;
  bool risingSeen;
  uint32_t lastRiseMs;
  float cycleMax;
  float cycleMin;
  uint8_t cycleCount;
  float cycleTu[AT_MAX_CYCLES];
  float cycleA[AT_MAX_CYCLES];
  float cycleOut[AT_MAX_CYCLES];
  float cycleOutSum;
  uint32_t cycleOutCount;
  float outHigh;
  float outLow;

  // result
  float bias;
  float outputMean;
  float tu;
  float a;
  float ku;
  float kp;
  float ki;
  float kd;
  float output;
};
