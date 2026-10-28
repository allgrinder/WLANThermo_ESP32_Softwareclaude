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

#include "Arduino.h"
#include "asyncHTTPrequest.h"

// Debug telemetry stream of the pitmaster for offline analysis
// (see docs/INSTALL-GAS.md and tools/analyze_cook.py).
//
// Design (Nano V3, ESP32):
//  - capture(): called by the system task right after the pitmaster update.
//    Copies one small record into a FreeRTOS queue without blocking. If the
//    queue is full the record is dropped and counted, the control loop never
//    waits for the network.
//  - update(): called by the connect task (1 s). Every TELEMETRY_SEND_S it
//    builds one JSON batch from the queue and posts it with its own async
//    HTTP client (independent of the cloud/notification queue).
//  - default off, interval 1..5 s

#define TELEMETRY_QUEUE_LEN 64u
#define TELEMETRY_SEND_S 10u
#define TELEMETRY_INTERVAL_MIN 1u
#define TELEMETRY_INTERVAL_MAX 5u

typedef struct
{
  boolean enabled;
  String url;
  uint8_t interval; // s
} TelemetryConfig;

typedef struct
{
  uint32_t ms;
  float raw;
  float temp;
  float set;
  float out;
  float p;
  float i;
  float d;
  float esum;
  uint16_t servoUs;
  uint8_t pm;
  uint8_t flags; // bit0 jump, bit1 lid, bit2 flame suspected, bit3 flame out, bit4 autotune running
  uint8_t atState;
  uint8_t atPhase;
  uint8_t atCycle;
  uint8_t type;
} TelemetrySample;

class Telemetry
{
public:
  Telemetry();
  void loadConfig();
  void saveConfig();
  TelemetryConfig getConfig() { return config; }
  void setConfig(TelemetryConfig newConfig);
  void capture();
  void reportLoopTime(uint32_t us);
  void update();
  uint32_t getDropped() { return dropped; }
  uint32_t getSent() { return sent; }

private:
  static void onReadyStateChange(void *optParm, asyncHTTPrequest *request, int readyState);
  TelemetryConfig config;
  QueueHandle_t queue;
  asyncHTTPrequest client;
  boolean requestDone;
  uint8_t captureCounter;
  uint8_t sendCounter;
  uint32_t dropped;
  uint32_t sent;
  uint32_t failed;
  volatile uint32_t loopUsMax;
};
