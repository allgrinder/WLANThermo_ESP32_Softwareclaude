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
#include "Telemetry.h"
#include "system/SystemBase.h"
#include "Settings.h"
#include "Version.h"
#include "TimeLib.h"
#include "ArduinoLog.h"

#define READY_STATE_DONE 4
#define TELEMETRY_HTTP_TIMEOUT_S 5

Telemetry::Telemetry()
{
  config.enabled = false;
  config.url = "";
  config.interval = 1u;
  queue = NULL;
  requestDone = true;
  captureCounter = 0u;
  sendCounter = 0u;
  dropped = 0u;
  sent = 0u;
  failed = 0u;
  loopUsMax = 0u;
}

void Telemetry::loadConfig()
{
  DynamicJsonBuffer jsonBuffer(Settings::jsonBufferSize);
  JsonObject &json = Settings::read(kTelemetry, &jsonBuffer);

  if (json.success())
  {
    if (json.containsKey("enabled"))
      config.enabled = json["enabled"];
    if (json.containsKey("url"))
      config.url = json["url"].asString();
    if (json.containsKey("interval"))
      config.interval = constrain(json["interval"].as<uint8_t>(), TELEMETRY_INTERVAL_MIN, TELEMETRY_INTERVAL_MAX);
  }
}

void Telemetry::saveConfig()
{
  DynamicJsonBuffer jsonBuffer(Settings::jsonBufferSize);
  JsonObject &json = jsonBuffer.createObject();
  json["enabled"] = config.enabled;
  json["url"] = config.url;
  json["interval"] = config.interval;
  Settings::write(kTelemetry, json);
}

void Telemetry::setConfig(TelemetryConfig newConfig)
{
  newConfig.interval = constrain(newConfig.interval, TELEMETRY_INTERVAL_MIN, TELEMETRY_INTERVAL_MAX);
  config = newConfig;
  saveConfig();
}

void Telemetry::reportLoopTime(uint32_t us)
{
  if (us > loopUsMax)
    loopUsMax = us;
}

// system task, once per second after the pitmaster update
void Telemetry::capture()
{
  if ((false == config.enabled) || (0u == config.url.length()))
    return;

  if (NULL == queue)
  {
    queue = xQueueCreate(TELEMETRY_QUEUE_LEN, sizeof(TelemetrySample));
    if (NULL == queue)
      return;
  }

  if (++captureCounter < config.interval)
    return;
  captureCounter = 0u;

  for (uint8_t index = 0u; index < gSystem->pitmasters.count(); index++)
  {
    Pitmaster *pm = gSystem->pitmasters[index];
    if (NULL == pm)
      continue;

    TemperatureBase *temperature = pm->getAssignedTemperature();
    AutoTuneRelay *at = pm->getAutoTune();
    PidCore *pid = pm->getPid();

    TelemetrySample sample;
    sample.ms = millis();
    sample.raw = (temperature != NULL) ? temperature->getValue() : INACTIVEVALUE;
    sample.temp = pm->getFilteredTemperature();
    sample.set = pm->getTargetTemperature();
    sample.out = pm->getValue();
    sample.p = pid->getP();
    sample.i = pid->getI();
    sample.d = pid->getD();
    sample.esum = pid->getEsum();
    sample.servoUs = pm->getServoPulseUs();
    sample.pm = index;
    sample.flags = (pid->isJump() ? 0x01u : 0u) |
                   (pm->getOPLStatus() ? 0x02u : 0u) |
                   (pm->isFlameOutSuspected() ? 0x04u : 0u) |
                   (pm->isFlameOut() ? 0x08u : 0u) |
                   (pm->isAutoTuneRunning() ? 0x10u : 0u);
    sample.atState = (at != NULL) ? (uint8_t)at->getState() : 0u;
    sample.atPhase = (at != NULL) ? (uint8_t)at->getPhase() : 0u;
    sample.atCycle = (at != NULL) ? at->getCycle() : 0u;
    sample.type = (uint8_t)pm->getType();

    // never block the control loop
    if (xQueueSend(queue, &sample, 0u) != pdTRUE)
      dropped++;
  }
}

void Telemetry::onReadyStateChange(void *optParm, asyncHTTPrequest *request, int readyState)
{
  Telemetry *telemetry = (Telemetry *)optParm;

  if (READY_STATE_DONE == readyState)
  {
    // the response is ignored on purpose (never parsed as server API)
    if (request->responseHTTPcode() == 200)
      telemetry->sent++;
    else
      telemetry->failed++;

    telemetry->requestDone = true;
  }
}

static void appendFloat(String &s, float value, uint8_t digits)
{
  s += String(value, (unsigned int)digits);
  s += ',';
}

// connect task, once per second
void Telemetry::update()
{
  if ((false == config.enabled) || (0u == config.url.length()) || (NULL == queue))
    return;

  if (++sendCounter < TELEMETRY_SEND_S)
    return;

  if (false == requestDone)
  {
    // previous batch still in flight, keep collecting
    return;
  }
  sendCounter = 0u;

  UBaseType_t waiting = uxQueueMessagesWaiting(queue);
  if (0u == waiting)
    return;

  String body;
  body.reserve(200u + waiting * 110u);
  body += "{\"sn\":\"";
  body += gSystem->getSerialNumber();
  body += "\",\"fw\":\"";
  body += FIRMWAREVERSION;
  body += "\",\"now\":";
  body += String((uint32_t)now());
  body += ",\"now_ms\":";
  body += String(millis());
  body += ",\"heap\":";
  body += String(ESP.getFreeHeap());
  body += ",\"loop_us_max\":";
  body += String(loopUsMax);
  body += ",\"dropped\":";
  body += String(dropped);
  body += ",\"failed\":";
  body += String(failed);
  body += ",\"interval\":";
  body += String(config.interval);
  body += ",\"cols\":[\"ms\",\"pm\",\"type\",\"raw\",\"temp\",\"set\",\"out\",\"servo_us\",\"p\",\"i\",\"d\",\"esum\",\"flags\",\"at_state\",\"at_phase\",\"at_cycle\"]";
  body += ",\"rows\":[";

  loopUsMax = 0u;

  TelemetrySample sample;
  boolean first = true;
  while (xQueueReceive(queue, &sample, 0u) == pdTRUE)
  {
    if (!first)
      body += ',';
    first = false;
    body += '[';
    body += String(sample.ms);
    body += ',';
    body += String(sample.pm);
    body += ',';
    body += String(sample.type);
    body += ',';
    appendFloat(body, sample.raw, 2);
    appendFloat(body, sample.temp, 2);
    appendFloat(body, sample.set, 1);
    appendFloat(body, sample.out, 2);
    body += String(sample.servoUs);
    body += ',';
    appendFloat(body, sample.p, 2);
    appendFloat(body, sample.i, 2);
    appendFloat(body, sample.d, 2);
    appendFloat(body, sample.esum, 1);
    body += String(sample.flags);
    body += ',';
    body += String(sample.atState);
    body += ',';
    body += String(sample.atPhase);
    body += ',';
    body += String(sample.atCycle);
    body += ']';
  }
  body += "]}";

  requestDone = false;
  client.onReadyStateChange(Telemetry::onReadyStateChange, this);
  client.setTimeout(TELEMETRY_HTTP_TIMEOUT_S);
  if (client.open("POST", config.url.c_str()))
  {
    client.setReqHeader("Connection", "close");
    client.setReqHeader("User-Agent", "WLANThermo ESP32 Telemetry");
    client.setReqHeader("Content-Type", "application/json");
    if (!client.send(body))
    {
      requestDone = true;
      failed++;
    }
  }
  else
  {
    requestDone = true;
    failed++;
  }
}
