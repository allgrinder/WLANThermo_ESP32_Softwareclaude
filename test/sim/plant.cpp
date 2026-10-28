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
#include "plant.h"
#include <math.h>
#include <algorithm>

#define SIM_INACTIVE 999.0

GasSmokerPlant::GasSmokerPlant(const PlantParams &params, double startTemp, uint32_t seed)
    : p(params), rng(seed), noiseDist(-params.noise, params.noise)
{
  lidOpen = false;
  flameOut = false;
  sensorLost = false;
  extraHeat = 0;
  chamber = startTemp;
  sensor = startTemp;
  delayIndex = 0;
  lastDt = 0;
}

double GasSmokerPlant::steadyState(double opening) const
{
  double u = std::min(100.0, std::max(0.0, opening)) / 100.0;
  return p.tMin + (p.tMax - p.tMin) * pow(u, p.valveExp);
}

void GasSmokerPlant::step(double dt, double opening)
{
  // dead time as ring buffer of the opening
  size_t len = (size_t)(p.deadTimeS / dt + 0.5);
  if (len < 1)
    len = 1;
  if (delayLine.size() != len)
  {
    delayLine.assign(len, opening);
    delayIndex = 0;
  }

  double delayed = delayLine[delayIndex];
  delayLine[delayIndex] = opening;
  delayIndex = (delayIndex + 1) % len;

  double target = flameOut ? p.tAmb : (steadyState(delayed) + extraHeat);
  chamber += dt * (target - chamber) / p.tauS;

  if (lidOpen)
    chamber += dt * (p.tAmb - chamber) / p.tauLidS;

  sensor += dt * (chamber - sensor) / p.tauSensorS;
}

double GasSmokerPlant::measure()
{
  if (sensorLost)
    return SIM_INACTIVE;

  return sensor + noiseDist(rng);
}
