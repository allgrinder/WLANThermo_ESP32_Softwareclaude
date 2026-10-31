/***************************************************
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
****************************************************/
// Host simulator replacement of src/temperature/TemperatureBase.h.
// Only the interface used by the pitmaster. update()/refresh() replicate
// the filtering of the real class (median of 9 samples at 5 Hz, refresh
// once per second with the gradient sign filter).
#pragma once

#include "Arduino.h"
#include "MedianFilterLib.h"

#define INACTIVEVALUE 999
#define SIM_MEDIAN_FLOAT_OFFSET 1000
#define SIM_TEMPERATURE_MEDIAN_SIZE 9u

enum class SensorType
{
  Maverick = 0,
  MaverickRadio = 99,
};

enum TemperatureUnit
{
  Fahrenheit = 'F',
  Celsius = 'C',
};

class TemperatureBase
{
public:
  TemperatureBase() : median(SIM_TEMPERATURE_MEDIAN_SIZE)
  {
    currentValue = INACTIVEVALUE;
    preValue = INACTIVEVALUE;
    currentGradient = 0;
    gradientSign = 0;
  }

  float getValue() { return currentValue; }
  float getPreValue() { return preValue; }
  int8_t getGradient() { return currentGradient; }
  boolean isActive() { return (INACTIVEVALUE != this->currentValue); }
  TemperatureUnit getUnit() { return Celsius; }
  uint8_t getType() { return (uint8_t)SensorType::Maverick; }

  // 5 Hz: raw sample of the ADC (INACTIVEVALUE if no sensor)
  void update(float raw) { median.AddValue(raw + SIM_MEDIAN_FLOAT_OFFSET); }

  // 1 Hz: same logic as TemperatureBase::refresh()
  void refresh()
  {
    this->preValue = this->currentValue;
    int8_t preGradientSign = this->gradientSign;

    float currentVal = median.GetFiltered() - SIM_MEDIAN_FLOAT_OFFSET;
    float gradient = (isActive() == true) ? decimalPlace(currentVal) - decimalPlace(this->preValue) : 0;
    this->gradientSign = (0 == gradient) ? 0 : (0 < gradient) ? 1 : -1;
    this->currentGradient = (0 == gradient) ? 0 : gradient / fabsf(gradient);

    if (INACTIVEVALUE == currentVal)
    {
      this->currentValue = INACTIVEVALUE;
    }
    else if (preGradientSign == gradientSign)
    {
      this->currentValue = currentVal;
    }
    else
    {
      this->currentValue = this->preValue;
    }
  }

private:
  float decimalPlace(float value) { return (((int)value * 10.0) / 10.0); }

  MedianFilter<float> median;
  float currentValue;
  float preValue;
  int8_t currentGradient;
  int8_t gradientSign;
};
