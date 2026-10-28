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

// Start values of the "Gas Servo" profile. Used by the firmware
// (SystemNanoV3.cpp) and by the host simulator (test/sim), see
// docs/autotune-simulation.md for how they were derived.
#define GAS_KP 1.5
#define GAS_KI 0.002
#define GAS_KD 60.0
