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
#include "FlameOutDetector.h"
#include <string.h>

FlameOutDetector::FlameOutDetector()
{
  this->drop = FO_DEFAULT_DROP;
  this->windowS = FO_DEFAULT_WINDOW_S;
  this->bias = 0;
  this->biasKnown = false;
  this->biasSamples = 0u;
  this->triggered = false;
  this->reset();
}

void FlameOutDetector::configure(float newDrop, uint16_t newWindowS)
{
  this->drop = (newDrop > 0.0f) ? newDrop : FO_DEFAULT_DROP;

  if (newWindowS < 60u)
    newWindowS = FO_DEFAULT_WINDOW_S;
  if (newWindowS > ((FO_MAX_SAMPLES - 1u) * FO_SAMPLE_INTERVAL_S))
    newWindowS = (FO_MAX_SAMPLES - 1u) * FO_SAMPLE_INTERVAL_S;

  this->windowS = newWindowS;
}

void FlameOutDetector::reset()
{
  memset(this->samples, 0, sizeof(this->samples));
  this->sampleCount = 0u;
  this->sampleHead = 0u;
  this->lastSampleMs = 0u;
  this->sampleStarted = false;
  this->lidHold = false;
  this->lidHoldUntilMs = 0u;
  this->outputHigh = false;
  this->outputHighSinceMs = 0u;
  this->suspected = false;
}

void FlameOutDetector::setBias(float newBias)
{
  this->bias = newBias;
  this->biasKnown = true;
  this->biasSamples = FO_BIAS_LEARN_S;
}

void FlameOutDetector::acknowledge()
{
  this->triggered = false;
  this->reset();
}

void FlameOutDetector::addSample(float x)
{
  this->samples[this->sampleHead] = x;
  this->sampleHead = (this->sampleHead + 1u) % FO_MAX_SAMPLES;
  if (this->sampleCount < FO_MAX_SAMPLES)
    this->sampleCount++;
}

bool FlameOutDetector::update(float x, float setpoint, float output, bool lidOpen, uint32_t nowMs)
{
  if (this->triggered)
    return true;

  // learn the steady state output (bias) while close to the setpoint
  if ((false == lidOpen) && (x >= (setpoint - FO_BIAS_BAND)) && (x <= (setpoint + FO_BIAS_BAND)))
  {
    if (this->biasSamples < FO_BIAS_LEARN_S)
    {
      this->biasSamples++;
      this->bias += (output - this->bias) / (float)this->biasSamples;
    }
    else
    {
      this->bias += (output - this->bias) / (float)FO_BIAS_LEARN_S;
      this->biasKnown = true;
    }
  }

  // sample history every FO_SAMPLE_INTERVAL_S
  if ((false == this->sampleStarted) || ((nowMs - this->lastSampleMs) >= (FO_SAMPLE_INTERVAL_S * 1000u)))
  {
    this->sampleStarted = true;
    this->lastSampleMs = nowMs;
    addSample(x);
  }

  // open lid: no detection during and one window after the lid event
  if (lidOpen)
  {
    this->lidHold = true;
    this->lidHoldUntilMs = nowMs + (this->windowS * 1000u);
  }
  else if (this->lidHold && ((int32_t)(nowMs - this->lidHoldUntilMs) >= 0))
  {
    this->lidHold = false;
  }

  // output condition must hold for some time
  float outputRef = this->biasKnown ? this->bias : FO_DEFAULT_OUTPUT_REF;
  if (output >= outputRef)
  {
    if (false == this->outputHigh)
    {
      this->outputHigh = true;
      this->outputHighSinceMs = nowMs;
    }
  }
  else
  {
    this->outputHigh = false;
  }

  // steady drop within the window
  uint8_t windowSamples = (uint8_t)(this->windowS / FO_SAMPLE_INTERVAL_S) + 1u;
  if (windowSamples > this->sampleCount)
    windowSamples = this->sampleCount;

  bool steadyDrop = false;

  if (windowSamples >= 2u)
  {
    // oldest sample of the window
    uint8_t start = (this->sampleHead + FO_MAX_SAMPLES - windowSamples) % FO_MAX_SAMPLES;
    float maxValue = this->samples[start];
    uint8_t maxIndex = 0u;

    for (uint8_t i = 1u; i < windowSamples; i++)
    {
      float value = this->samples[(start + i) % FO_MAX_SAMPLES];
      if (value >= maxValue)
      {
        maxValue = value;
        maxIndex = i;
      }
    }

    if ((maxValue - x) > this->drop)
    {
      steadyDrop = true;
      float previous = maxValue;

      for (uint8_t i = maxIndex + 1u; i < windowSamples; i++)
      {
        float value = this->samples[(start + i) % FO_MAX_SAMPLES];
        if (value > (previous + FO_RISE_TOLERANCE))
        {
          steadyDrop = false;
          break;
        }
        previous = value;
      }

      if (x > (previous + FO_RISE_TOLERANCE))
        steadyDrop = false;
    }
  }

  this->suspected = steadyDrop && (x < setpoint);

  if (this->suspected && (false == this->lidHold) && this->outputHigh &&
      ((nowMs - this->outputHighSinceMs) >= (FO_OUTPUT_HOLD_S * 1000u)))
  {
    this->triggered = true;
  }

  return this->triggered;
}
