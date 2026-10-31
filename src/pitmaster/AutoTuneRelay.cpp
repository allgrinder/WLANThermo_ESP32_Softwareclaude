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
#include "AutoTuneRelay.h"
#include <string.h>

#define AT_PI 3.14159265f

// Sequence
//  1. Approach: the regular PID drives to the setpoint. As soon as the
//     temperature stays within +-stableBand for stableS, the mean output of
//     that time becomes the bias. Fallback after approachTimeoutS: bias = 50 %.
//  2. Relay: output = bias + d while x < setpoint - h was seen last,
//     bias - d after x > setpoint + h (limited to minOutput..maxOutput).
//  3. A cycle is the time between two switches to the high output. Per
//     cycle the period Tu and the amplitude a = (max - min) / 2 are recorded.
//     Done after at least minCycles when the last 3 cycles differ by less than
//     tolerance in Tu and a, at the latest after maxCycles.
//  4. Ku = 4 * d / (pi * a), with d = (outHigh - outLow) / 2 (the effective
//     relay amplitude after limiting).

AutoTuneRelay::AutoTuneRelay()
{
  memset(&this->config, 0, sizeof(this->config));
  defaultConfig(this->config);
  this->state = AtState::Idle;
  this->phase = AtPhase::None;
  this->error = AtError::None;
  this->warnings = 0u;
  this->startMs = 0u;
  this->cycleCount = 0u;
  this->bias = 0;
  this->outputMean = 0;
  this->tu = 0;
  this->a = 0;
  this->ku = 0;
  this->kp = 0;
  this->ki = 0;
  this->kd = 0;
  this->output = 0;
}

void AutoTuneRelay::defaultConfig(AtConfig &config)
{
  config.d = 20.0f;
  config.h = 1.5f;
  config.minOutput = 0.0f;
  config.maxOutput = 100.0f;
  config.rule = AtRule::TyreusLuybenPid;
  config.overTemp = 25.0f;
  config.timeoutS = 90u * 60u;
  config.stableBand = 3.0f;
  config.stableS = 5u * 60u;
  config.approachTimeoutS = 30u * 60u;
  config.minCycles = 4u;
  config.maxCycles = AT_MAX_CYCLES;
  config.tolerance = 0.15f;
  config.halfCycleTimeoutS = 20u * 60u;
}

void AutoTuneRelay::start(const AtConfig &newConfig, uint32_t nowMs)
{
  this->config = newConfig;

  if (this->config.maxCycles > AT_MAX_CYCLES)
    this->config.maxCycles = AT_MAX_CYCLES;
  if (this->config.minCycles < 3u)
    this->config.minCycles = 3u;
  if (this->config.minCycles > this->config.maxCycles)
    this->config.minCycles = this->config.maxCycles;
  if (this->config.minOutput < 0.0f)
    this->config.minOutput = 0.0f;
  if (this->config.maxOutput > 100.0f)
    this->config.maxOutput = 100.0f;

  this->state = AtState::Running;
  this->phase = AtPhase::Approach;
  this->error = AtError::None;
  this->warnings = 0u;
  this->startMs = nowMs;

  this->inBand = false;
  this->bandStartMs = nowMs;
  this->outputSum = 0;
  this->outputCount = 0u;

  this->relayHigh = true;
  this->lastSwitchMs = nowMs;
  this->risingSeen = false;
  this->lastRiseMs = nowMs;
  this->cycleMax = -1000.0f;
  this->cycleMin = 1000.0f;
  this->cycleCount = 0u;
  memset(this->cycleTu, 0, sizeof(this->cycleTu));
  memset(this->cycleA, 0, sizeof(this->cycleA));
  memset(this->cycleOut, 0, sizeof(this->cycleOut));
  this->cycleOutSum = 0;
  this->cycleOutCount = 0u;
  this->outHigh = 0;
  this->outLow = 0;

  this->bias = 0;
  this->outputMean = 0;
  this->tu = 0;
  this->a = 0;
  this->ku = 0;
  this->kp = 0;
  this->ki = 0;
  this->kd = 0;
  this->output = this->config.minOutput;
}

float AutoTuneRelay::clampOutput(float value) const
{
  if (value < this->config.minOutput)
    value = this->config.minOutput;
  if (value > this->config.maxOutput)
    value = this->config.maxOutput;
  return value;
}

void AutoTuneRelay::abort(AtError reason)
{
  if (AtState::Running != this->state)
    return;

  this->state = AtState::Aborted;
  this->error = reason;
  this->output = this->config.minOutput;
}

void AutoTuneRelay::markRestartAbort(float setpoint)
{
  this->config.setpoint = setpoint;
  this->state = AtState::Aborted;
  this->phase = AtPhase::None;
  this->error = AtError::Restart;
  this->output = this->config.minOutput;
}

void AutoTuneRelay::finishApproach(float newBias, uint32_t nowMs)
{
  // keep room for the relay amplitude where possible
  this->bias = clampOutput(newBias);
  this->outHigh = clampOutput(this->bias + this->config.d);
  this->outLow = clampOutput(this->bias - this->config.d);
  this->phase = AtPhase::Relay;
  this->lastSwitchMs = nowMs;
  this->risingSeen = false;
  this->cycleMax = -1000.0f;
  this->cycleMin = 1000.0f;
}

void AutoTuneRelay::onRisingSwitch(uint32_t nowMs)
{
  if (this->risingSeen && (this->cycleCount < this->config.maxCycles))
  {
    this->cycleTu[this->cycleCount] = (nowMs - this->lastRiseMs) / 1000.0f;
    this->cycleA[this->cycleCount] = (this->cycleMax - this->cycleMin) / 2.0f;
    this->cycleOut[this->cycleCount] = (this->cycleOutCount > 0u) ? (this->cycleOutSum / this->cycleOutCount) : this->bias;
    this->cycleCount++;
  }

  this->cycleOutSum = 0;
  this->cycleOutCount = 0u;

  this->risingSeen = true;
  this->lastRiseMs = nowMs;
}

static float relativeSpread(const float *values, uint8_t count)
{
  float minValue = values[0];
  float maxValue = values[0];
  float sum = 0;

  for (uint8_t i = 0u; i < count; i++)
  {
    if (values[i] < minValue)
      minValue = values[i];
    if (values[i] > maxValue)
      maxValue = values[i];
    sum += values[i];
  }

  float mean = sum / count;
  return (mean > 0.0f) ? ((maxValue - minValue) / mean) : 1.0f;
}

bool AutoTuneRelay::lastCyclesConverged() const
{
  if (this->cycleCount < this->config.minCycles)
    return false;

  const float *tuLast = &this->cycleTu[this->cycleCount - 3u];
  const float *aLast = &this->cycleA[this->cycleCount - 3u];

  return (relativeSpread(tuLast, 3u) < this->config.tolerance) &&
         (relativeSpread(aLast, 3u) < this->config.tolerance);
}

void AutoTuneRelay::computeGains(AtRule rule, float ku, float tuS, float &kp, float &ki, float &kd)
{
  // Unit derivation for this firmware (see PidCore::calc):
  //   y = kp*e + ki*esum + kd*edif
  //   esum = sum(e * dt) with dt in seconds        -> integral in °C*s
  //   edif = (e - e_old) / (dCount * dt)           -> slope in °C/s
  // The textbook ideal (parallel) form is
  //   y = Kp * (e + 1/Ti * integral(e dt) + Td * de/dt)
  // with Ti, Td in seconds. Matching term by term:
  //   kp = Kp            [%/°C]
  //   ki = Kp / Ti       [%/(°C*s)]
  //   kd = Kp * Td       [%*s/°C]
  // Ku from the relay test is in % per °C because d is in % and a in °C,
  // so Kp derived from it is directly in the unit the firmware expects.
  float Kp = 0;
  float Ti = 0;
  float Td = 0;

  switch (rule)
  {
  case AtRule::ZieglerNicholsNoOvershootPid:
    Kp = 0.2f * ku;
    Ti = 0.5f * tuS;
    Td = tuS / 3.0f;
    break;
  case AtRule::TyreusLuybenPi:
    Kp = ku / 3.2f;
    Ti = 2.2f * tuS;
    Td = 0.0f;
    break;
  case AtRule::TyreusLuybenPid:
  default:
    Kp = ku / 2.2f;
    Ti = 2.2f * tuS;
    Td = tuS / 6.3f;
    break;
  }

  kp = Kp;
  ki = (Ti > 0.0f) ? (Kp / Ti) : 0.0f;
  kd = Kp * Td;
}

void AutoTuneRelay::computeResult()
{
  uint8_t first = this->cycleCount - 3u;
  float tuSum = 0;
  float aSum = 0;
  float outSum = 0;

  for (uint8_t i = first; i < this->cycleCount; i++)
  {
    tuSum += this->cycleTu[i];
    aSum += this->cycleA[i];
    outSum += this->cycleOut[i];
  }

  this->tu = tuSum / 3.0f;
  this->a = aSum / 3.0f;
  // The time mean of a relay output overestimates the steady state output
  // for a concave valve characteristic (gas: heat ~ sqrt(opening)), the bias
  // from the approach is often closer. Take the smaller one, the PID
  // integral corrects the rest.
  this->outputMean = outSum / 3.0f;
  if (this->bias < this->outputMean)
    this->outputMean = this->bias;

  float dEff = (this->outHigh - this->outLow) / 2.0f;

  if ((this->a <= 0.0f) || (dEff <= 0.0f) || (this->tu <= 0.0f))
  {
    abort(AtError::NoOscillation);
    return;
  }

  this->ku = (4.0f * dEff) / (AT_PI * this->a);
  computeGains(this->config.rule, this->ku, this->tu, this->kp, this->ki, this->kd);

  this->state = AtState::Done;
  this->phase = AtPhase::Finished;
}

float AutoTuneRelay::update(float x, bool sensorActive, float pidOutput, uint32_t nowMs)
{
  if (AtState::Running != this->state)
    return this->output;

  // safety checks first
  if (false == sensorActive)
  {
    abort(AtError::SensorLost);
    return this->output;
  }

  if (x > (this->config.setpoint + this->config.overTemp))
  {
    abort(AtError::OverTemp);
    return this->output;
  }

  if ((nowMs - this->startMs) >= (this->config.timeoutS * 1000u))
  {
    abort(AtError::Timeout);
    return this->output;
  }

  if (AtPhase::Approach == this->phase)
  {
    this->output = clampOutput(pidOutput);

    bool insideBand = (x >= (this->config.setpoint - this->config.stableBand)) &&
                      (x <= (this->config.setpoint + this->config.stableBand));

    if (insideBand)
    {
      if (false == this->inBand)
      {
        this->inBand = true;
        this->bandStartMs = nowMs;
        this->outputSum = 0;
        this->outputCount = 0u;
      }

      this->outputSum += this->output;
      this->outputCount++;

      if ((nowMs - this->bandStartMs) >= (this->config.stableS * 1000u))
      {
        finishApproach(this->outputSum / this->outputCount, nowMs);
      }
    }
    else
    {
      this->inBand = false;
    }

    if ((AtPhase::Approach == this->phase) && ((nowMs - this->startMs) >= (this->config.approachTimeoutS * 1000u)))
    {
      this->warnings |= AT_WARN_BIAS_FALLBACK;
      finishApproach(50.0f, nowMs);
    }

    if (AtPhase::Relay == this->phase)
    {
      // start the relay in the direction that brings x back to the setpoint
      this->relayHigh = (x < this->config.setpoint);
      this->output = this->relayHigh ? this->outHigh : this->outLow;
    }

    return this->output;
  }

  // relay phase
  if (x > this->cycleMax)
    this->cycleMax = x;
  if (x < this->cycleMin)
    this->cycleMin = x;

  if (this->relayHigh && (x > (this->config.setpoint + this->config.h)))
  {
    this->relayHigh = false;
    this->lastSwitchMs = nowMs;
  }
  else if ((false == this->relayHigh) && (x < (this->config.setpoint - this->config.h)))
  {
    this->relayHigh = true;
    this->lastSwitchMs = nowMs;
    onRisingSwitch(nowMs);
    // the new cycle starts here
    this->cycleMax = x;
    this->cycleMin = x;

    if (lastCyclesConverged())
    {
      computeResult();
    }
    else if (this->cycleCount >= this->config.maxCycles)
    {
      this->warnings |= AT_WARN_NOT_CONVERGED;
      computeResult();
    }

    if (AtState::Running != this->state)
    {
      // done or aborted: hand over at the mean output (done) or minOutput (aborted)
      this->output = (AtState::Done == this->state) ? clampOutput(this->outputMean) : this->config.minOutput;
      return this->output;
    }
  }

  if ((nowMs - this->lastSwitchMs) >= (this->config.halfCycleTimeoutS * 1000u))
  {
    abort(AtError::NoOscillation);
    return this->output;
  }

  this->output = this->relayHigh ? this->outHigh : this->outLow;

  // output mean of the running cycle (called once per controller cycle)
  this->cycleOutSum += this->output;
  this->cycleOutCount++;

  return this->output;
}
