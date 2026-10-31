/***************************************************
    Copyright (C) 2016  Steffen Ochs
    Copyright (C) 2019  Martin Koerner

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
// Verbatim copy of Pitmaster::pidCalc() from upstream master (v1.2.4),
// with the member variables moved into a struct. Used to prove that
// PidCore::calc() computes exactly the same values.
#pragma once
#include <stdint.h>

struct RefPid
{
  float esum = 0, elast = 0, Ki_alt = 0;
  uint8_t ecount = 15, dCount = 15;
  float edif = 0;
  bool jump = 0;
  uint8_t ampch = 0;
  uint16_t pause = 1000;

  float calc(float x, float w, float kp, float ki, float kd, bool ssr)
  {
    int32_t diff;
    float e;
    if (ssr)
    {
      diff = (w - x) * 100;
      e = diff / 100.0;
    }
    else
    {
      diff = (w - x) * 10;
      e = diff / 10.0;
    }
    float jpthres = 100u;
    float jumpth = 1000u;
    if (x < jpthres)
      jumpth = 25u;
    if (e > jumpth)
    {
      this->jump = true;
      this->ampch = 1;
    }
    if (true == this->jump)
    {
      if (this->ampch == 1 && e <= 0)
        this->ampch = 2;
      else if (this->ampch == 2 && e > 0)
        this->ampch = 3;
    }
    if (this->ampch == 3)
      this->jump = false;

    float p_out = kp * e;
    this->ecount++;
    if (this->ecount >= this->dCount)
    {
      this->edif = (e - this->elast) / (this->pause / 1000.0);
      this->edif = this->edif / (float)this->dCount;
      this->elast = e;
      this->ecount = 0u;
    }
    float d_out = kd * edif;
    float i_out;
    if (ki != 0)
    {
      if (ki != this->Ki_alt)
      {
        this->esum = (this->esum * this->Ki_alt) / ki;
        this->Ki_alt = ki;
      }
      if (p_out < 100)
        this->esum += e * (this->pause / 1000.0);
      if (this->esum * ki > 95)
        this->esum = 95 / ki;
      else if (this->esum * ki < 0)
        this->esum = 0 / ki;
      i_out = ki * this->esum;
    }
    else
    {
      this->esum = 0;
      i_out = 0;
      this->Ki_alt = 0;
    }
    float y = p_out + i_out + d_out;
    y = (y < 0) ? 0 : ((y > 100) ? 100 : y);
    return y;
  }
};
