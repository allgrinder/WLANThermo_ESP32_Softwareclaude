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
// Gas smoker plant model for the host simulator.
//
//  valve:   opening u in % of the servo travel between "smallest stage"
//           (u = 0) and "big flame" (u = 100). Heat power is not linear:
//           Tss(u) = tAmb + (tMin - tAmb) + (tMax - tMin) * (u/100)^valveExp
//           with valveExp = 0.5 (sqrt): close to the smallest stage a small
//           servo move changes a lot. Above valveSat more opening brings no
//           more heat (u is scaled by valveSat and capped), as measured on the
//           El Fuego with water pan (docs/cooks/2026-10-30).
//  chamber: first order with time constant tau and dead time L (FOPDT)
//           tau * dT/dt = Tss(u(t - L)) - T
//  sensor:  first order lag tauSensor, noise +-noise (uniform)
//  disturbances: lid open (extra loss to ambient with tauLid), flame out
//           (heat power 0, gas still flows), extra heat (e.g. fat fire)
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <random>

struct PlantParams
{
  std::string name;
  double deadTimeS;  // L
  double tauS;       // chamber time constant
  double tAmb;       // ambient
  double tMin;       // steady state temperature at the smallest stage
  double tMax;       // steady state temperature with big flame
  double valveExp;   // valve characteristic exponent
  double valveSat;   // opening in % from which on the heat saturates (100 = no saturation)
  double tauSensorS; // sensor lag
  double noise;      // +- uniform measurement noise
  double tauLidS;    // time constant towards ambient while the lid is open
};

class GasSmokerPlant
{
public:
  GasSmokerPlant(const PlantParams &params, double startTemp, uint32_t seed);

  // dt in s, opening in % (0..100)
  void step(double dt, double opening);
  // measured value as the ADC would deliver it (no filtering)
  double measure();

  double getChamber() const { return chamber; }
  double getSensor() const { return sensor; }
  double steadyState(double opening) const;

  bool lidOpen;
  bool flameOut;
  bool sensorLost;
  double extraHeat; // additional steady state temperature in °C

private:
  PlantParams p;
  double chamber;
  double sensor;
  std::vector<double> delayLine;
  size_t delayIndex;
  double lastDt;
  std::mt19937 rng;
  std::uniform_real_distribution<double> noiseDist;
};
