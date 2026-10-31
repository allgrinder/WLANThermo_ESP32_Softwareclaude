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
#include "BiasMap.h"

static float binTemp(uint8_t bin)
{
  return BIASMAP_T_MIN + bin * BIASMAP_T_STEP;
}

static float clampOutput(float v)
{
  return (v < 0.0f) ? 0.0f : ((v > 100.0f) ? 100.0f : v);
}

BiasMap::BiasMap()
{
  profileId = 0xFFu;
  clear();
}

void BiasMap::clear()
{
  for (uint8_t i = 0u; i < BIASMAP_BINS; i++)
  {
    value[i] = 0;
    learned[i] = 0u;
  }
}

void BiasMap::setBin(uint8_t bin, float output, uint16_t learnedS)
{
  if (bin < BIASMAP_BINS)
  {
    value[bin] = clampOutput(output);
    learned[bin] = learnedS;
  }
}

uint8_t BiasMap::knownCount() const
{
  uint8_t n = 0u;
  for (uint8_t i = 0u; i < BIASMAP_BINS; i++)
    if (isKnown(i))
      n++;
  return n;
}

bool BiasMap::learn(float temp, float output)
{
  float pos = (temp - BIASMAP_T_MIN) / BIASMAP_T_STEP;
  if ((pos < 0.0f) || (pos > (BIASMAP_BINS - 1)))
    return false;

  uint8_t lo = (uint8_t)pos;
  float frac = pos - lo;
  bool becameKnown = false;

  // the setpoint lies between two bins: both learn, weighted by distance
  for (uint8_t k = 0u; k < 2u; k++)
  {
    uint8_t bin = lo + k;
    float weight = (0u == k) ? (1.0f - frac) : frac;
    if ((bin >= BIASMAP_BINS) || (weight < 0.05f))
      continue;

    bool wasKnown = isKnown(bin);
    if (0u == learned[bin])
      value[bin] = output;
    else
      value[bin] += weight * (output - value[bin]) / BIASMAP_LEARN_TAU_S;

    if (learned[bin] < 60000u)
      learned[bin]++;

    if (!wasKnown && isKnown(bin))
      becameKnown = true;
  }

  return becameKnown;
}

void BiasMap::seed(float temp, float output)
{
  float pos = (temp - BIASMAP_T_MIN) / BIASMAP_T_STEP;
  if ((pos < 0.0f) || (pos > (BIASMAP_BINS - 1)))
    return;

  uint8_t bin = (uint8_t)(pos + 0.5f);
  // seed only bins that are not learned better already
  if (!isKnown(bin))
  {
    value[bin] = clampOutput(output);
    learned[bin] = BIASMAP_KNOWN_S;
  }
}

bool BiasMap::estimate(float temp, float &output) const
{
  int8_t below = -1;
  int8_t above = -1;

  for (uint8_t i = 0u; i < BIASMAP_BINS; i++)
  {
    if (!isKnown(i))
      continue;
    if (binTemp(i) <= temp)
      below = i;
    if ((binTemp(i) >= temp) && (above < 0))
      above = i;
  }

  if ((below < 0) && (above < 0))
    return false;

  // inside the known range: linear interpolation
  if ((below >= 0) && (above >= 0))
  {
    if (below == above)
    {
      output = value[below];
      return true;
    }
    float f = (temp - binTemp(below)) / (binTemp(above) - binTemp(below));
    output = clampOutput(value[below] + f * (value[above] - value[below]));
    return true;
  }

  // outside: slope of the two nearest known bins, limited range
  int8_t nearest = (below >= 0) ? below : above;
  int8_t second = -1;
  if (below >= 0)
  {
    for (int8_t i = below - 1; i >= 0; i--)
      if (isKnown(i))
      {
        second = i;
        break;
      }
  }
  else
  {
    for (int8_t i = above + 1; i < BIASMAP_BINS; i++)
      if (isKnown(i))
      {
        second = i;
        break;
      }
  }

  float tn = binTemp(nearest);
  float distance = temp - tn;
  if (distance > BIASMAP_EXTRAPOLATE_MAX)
    distance = BIASMAP_EXTRAPOLATE_MAX;
  if (distance < -BIASMAP_EXTRAPOLATE_MAX)
    distance = -BIASMAP_EXTRAPOLATE_MAX;

  if (second >= 0)
  {
    float slope = (value[nearest] - value[second]) / (tn - binTemp(second));
    if (slope < 0.0f)
      slope = 0.0f; // hotter never needs less gas
    output = clampOutput(value[nearest] + slope * distance);
    return true;
  }

  // only one point known: heat loss grows with (T - ambient), the valve
  // gives more heat per % at small openings (gas: roughly sqrt), so the
  // output grows roughly with (T - ambient)^2
  float ratio = (tn + distance - BIASMAP_AMBIENT) / (tn - BIASMAP_AMBIENT);
  if (ratio < 0.0f)
    ratio = 0.0f;
  output = clampOutput(value[nearest] * ratio * ratio);
  return true;
}
