/***************************************************
    Copyright (C) 2016  Steffen Ochs
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

    HISTORY: Please refer Github History

****************************************************/
#include "PidCore.h"

#define PIDKIMAX 95 // ANTI WINDUP LIMIT MAX
#define PIDKIMIN 0  // ANTI WINDUP LIMIT MIN
#define PITMIN 0    // LOWER LIMIT SET
#define PITMAX 100  // UPPER LIMIT SET

#define PID_DEFAULT_DCOUNT 15u

#define JP_THRESHOLD_RANGE 25u
#define JP_THRESHOLD_RANGE_OUT 1000u

PidCore::PidCore()
{
    this->ecount = PID_DEFAULT_DCOUNT;
    this->dCount = PID_DEFAULT_DCOUNT;
    this->edif = 0;
    this->pOut = 0;
    this->iOut = 0;
    this->dOut = 0;
    this->reset();
}

void PidCore::reset()
{
    this->esum = 0;
    this->elast = 0;
    this->Ki_alt = 0;
    this->jump = 0;
    this->ampch = 0;
}

void PidCore::preloadIntegral(float iOut, float ki)
{
    if (ki > 0.0f)
    {
        if (iOut > PIDKIMAX)
            iOut = PIDKIMAX;
        if (iOut < PIDKIMIN)
            iOut = PIDKIMIN;
        this->Ki_alt = ki;
        this->esum = iOut / ki;
    }
}

void PidCore::setDCount(uint8_t dCount)
{
    this->dCount = dCount;
    this->ecount = dCount;
}

float PidCore::calc(float x, float w, float kp, float ki, float kd, bool highResolution,
                    float jumpThreshold, float dtMs, bool jumpEnabled)
{
    // see: http://rn-wissen.de/wiki/index.php/Regelungstechnik
    // see: http://www.ni.com/white-paper/3782/en/

    int32_t diff;
    float e;

    // Abweichung bestimmen
    if (highResolution)
    {
        diff = (w - x) * 100;
        e = diff / 100.0;
    }
    else
    {
        diff = (w - x) * 10;
        e = diff / 10.0; // nur Temperaturunterschiede von >0.1°C beachten
    }

    // JUMP DETECTION
    if (jumpEnabled)
    {
        float jumpth = JP_THRESHOLD_RANGE_OUT; // unendlich
        if (x < jumpThreshold)
        {
            jumpth = JP_THRESHOLD_RANGE;
        }
        if (e > jumpth)
        {
            this->jump = true;
            this->ampch = 1;
        }

        // Erkennung Wellenform
        if (true == this->jump)
        {
            if (this->ampch == 1 && e <= 0)
            {
                this->ampch = 2;
            }
            else if (this->ampch == 2 && e > 0)
            {
                this->ampch = 3;
            }
        }

        // JUMP DEACTIVATION
        if (this->ampch == 3) // Deaktivierung sobald eine Welle durchlaufen
        {
            this->jump = false;
        }
    }
    else
    {
        this->clearJump();
    }

    // Proportional-Anteil
    float p_out = kp * e;

    // Differential-Anteil (Intervall-Berechnung)
    this->ecount++;
    if (this->ecount >= this->dCount)
    {
        this->edif = (e - this->elast) / (dtMs / 1000.0);
        this->edif = this->edif / (float)this->dCount;
        this->elast = e;
        this->ecount = 0u;
    }

    float d_out = kd * edif;

    // i-Anteil wechsl: https://github.com/WLANThermo/WLANThermo_v2/blob/b7bd6e1b56fe5659e8750c17c6dd1cd489872f6c/software/usr/sbin/wlt_2_pitmaster.py
    // Integral-Anteil
    float i_out;
    if (ki != 0)
    {
        // Sprünge im Reglerausgangswert bei Anpassung von Ki vermeiden
        if (ki != this->Ki_alt)
        {
            this->esum = (this->esum * this->Ki_alt) / ki;
            this->Ki_alt = ki;
        }

        // Anti-Windup I-Anteil
        // Keine Erhöhung I-Anteil wenn Regler bereits an der Grenze ist
        if (p_out < PITMAX)
        {
            this->esum += e * (dtMs / 1000.0);
        }

        // Anti-Windup I-Anteil (Limits)
        if (this->esum * ki > PIDKIMAX)
            this->esum = PIDKIMAX / ki;
        else if (this->esum * ki < PIDKIMIN)
            this->esum = PIDKIMIN / ki;

        i_out = ki * this->esum;
    }
    else
    {
        // Historie vergessen, da wir nach Ki = 0 von 0 aus anfangen
        this->esum = 0;
        i_out = 0;
        this->Ki_alt = 0;
    }

    this->pOut = p_out;
    this->iOut = i_out;
    this->dOut = d_out;

    // PID-Regler berechnen
    float y = p_out + i_out + d_out;
    // Auflösung am Ausgang ist begrenzt
    if (y < PITMIN)
        y = PITMIN;
    else if (y > PITMAX)
        y = PITMAX;

    return y;
}
