/*************************************************** 
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
    
    HISTORY: Please refer Github History
    
****************************************************/
#pragma once

#include "Arduino.h"
#include "temperature/TemperatureBase.h"
#include "MedianFilterLib.h"
#include "PidCore.h"
#include "AutoTuneRelay.h"
#include "FlameOutDetector.h"

#define SERVOPULSMIN 550u
#define SERVOPULSMAX 2250u

typedef struct TPitmasterProfile
{
  String name;
  uint8_t id;
  uint8_t actuator;
  float kp;
  float ki;
  float kd;
  float dcmin; // Duty Cycle Min (FAN / SSR)
  float dcmax; // Duty Cylce Max (FAM / SSR)
  byte jumppw;
  float spmin; // Servo Pulse Min
  float spmax; // Servo Pulse Max
  byte link;   // Link between Actuators
  byte opl;
  byte autotune;
  // gas fork extensions, appended so that existing initializers stay valid
  float minOutput;   // minimum output in % for PID, autotune and open lid (0..100)
  float atD;         // autotune relay amplitude in % (0 = default 20)
  float atH;         // autotune relay hysteresis in °C (0 = default 1.5)
  uint8_t atRule;    // autotune tuning rule (AtRule)
  uint8_t flameOut;  // flame out detection enabled
  float foDrop;      // flame out: temperature drop in °C (0 = default 15)
  uint16_t foWindow; // flame out: time window in s (0 = default 300)
} PitmasterProfile;

typedef struct TDutyCycleTest
{
  unsigned long timer; // SHUTDOWN TIMER
  uint16_t value;      // TEST VALUE * 10
  bool dc;             // WHICH DC: min or max
  uint8_t actuator;    // WHICH ACTOR
  int8_t saved;        // PITMASTER ACTIVITY CACHE (-1 .. 100)
} DutyCycleTest;

typedef struct TOpenLid
{
  bool detected;                            // Open Lid Detected
  float ref;                                // Open Lid Temperatur Memory
  float temp;                               // Temperatur by Open Lid
  int count;                                // Open Lid Count
  int fall_c;                               // Count falling events
} OpenLid;

typedef void (*PitmasterCallback_t)(class Pitmaster *, boolean, void *);

enum PitmasterType
{
  pm_off = 0,
  pm_manual = 1,
  pm_auto = 2,
};

enum PitMasterActuator
{
  SSR,
  FAN,
  SERVO,
  DAMPER,
  NOAR
};

#define PITMASTER_NO_SUPPLY_IO 0xFFu

// pitmaster events, collected by PitmasterGrp
#define PM_EVENT_SAVE 0x01u        // configuration changed, save required
#define PM_EVENT_AT_FINISHED 0x02u // autotune finished successfully
#define PM_EVENT_AT_ABORTED 0x04u  // autotune aborted
#define PM_EVENT_FLAMEOUT 0x08u    // flame out detected

// time to wait for the servo to reach the park position before detaching
#define SERVO_PARK_TIME_MS 1500u

class Pitmaster
{
public:
  Pitmaster(uint8_t ioPin1, uint8_t channel1, uint8_t ioPin2, uint8_t channel2);
  void setType(PitmasterType type);
  PitmasterType getType();
  void setTypeLast(PitmasterType type);
  PitmasterType getTypeLast();
  void assignProfile(PitmasterProfile *profile);
  PitmasterProfile *getAssignedProfile();
  void assignTemperature(TemperatureBase *temperature);
  TemperatureBase *getAssignedTemperature();
  float getValue();
  void setValue(float value);
  void setTargetTemperature(float temperature);
  float getTargetTemperature();
  void setServoMinDutyCyle(uint16_t dutyCycle);
  uint16_t getServoMinDutyCyle() { return this->servoDcMin; }
  void setServoMaxDutyCyle(uint16_t dutyCycle);
  uint16_t getServoMaxDutyCyle() { return this->servoDcMax; }
  void setDCount(uint8_t dutyCycle);
  uint8_t getDCount() { return this->pid.getDCount(); }
  boolean getOPLStatus();
  float getOPLTemperature();
  uint8_t getGlobalIndex() { return this->globalIndex; };
  bool startDutyCycleTest(uint8_t actuator, uint8_t value);
  bool startAutoTune();
  void stopAutoTune(AtError reason = AtError::UserStop);
  AutoTuneRelay *getAutoTune() { return this->autoTune; }
  void markAutoTuneRestartAbort();
  float pidCalc();
  void pidReset();
  PidCore *getPid() { return &this->pid; }
  void disableActuators(boolean allowdelay);
  void serviceSafePark();
  boolean isParking() { return this->parkPending; }
  boolean isActuatorAttached() { return this->initActuator != NOAR; }
  boolean isDutyCycleTestRunning();
  boolean isAutoTuneRunning();
  boolean isFlameOut() { return this->flameOutDetector.isTriggered(); }
  boolean isFlameOutSuspected() { return this->flameOutDetector.isSuspected(); }
  void acknowledgeFlameOut();
  float getMinOutput();
  float getFilteredTemperature() { return this->filteredTemp; }
  uint16_t getServoPulseUs();
  uint8_t takeEvents();
  void registerCallback(PitmasterCallback_t callback, void *userData);
  void unregisterCallback();
  void handleCallbacks();
  static void setSupplyPin(uint8_t ioPin);
  void virtual update();

protected:
private:
  boolean checkPause();
  boolean checkDutyCycleTest();
  boolean checkAutoTune();
  boolean checkOpenLid();
  float filterTemperature();
  float pidCalcFiltered(float x, bool jumpEnabled);
  void abortAutoTune(AtError reason);
  void startServoPark();
  void detachActuators();
  void updateFlameOut();
  void controlActuators();
  void initActuators();
  void controlFan(float newValue, float newDcMin, float newDcMax);
  void controlServo(float newValue, float newSPMin, float newSPMax);
  void controlSSR(float newValue, float newDcMin, float newDcMax);
  void enableStepUp(boolean enable);
  PitmasterType type;
  PitmasterType typeLast;
  PitmasterProfile *profile;
  TemperatureBase *temperature;
  DutyCycleTest *dutyCycleTest;
  AutoTuneRelay *autoTune;
  FlameOutDetector flameOutDetector;
  PidCore pid;
  OpenLid openLid;
  float targetTemperature;
  uint8_t ioPin1;
  uint8_t ioPin2;
  uint8_t channel1;
  uint8_t channel2;
  PitMasterActuator initActuator;
  PitmasterCallback_t registeredCb;
  boolean settingsChanged;
  void *registeredCbUserData;
  float cbValue;
  MedianFilter<float> *medianValue;

  // all pitmasters objects will share one supply IO
  static uint8_t ioSupply;
  static uint8_t ioSupplyRequested;

  static uint8_t globalIndexTracker;
  uint8_t globalIndex;

  float value;

  float filteredTemp;
  boolean medianPrimed;
  boolean parkPending;
  uint32_t parkUntil;
  uint8_t events;

  uint16_t pause;
  uint previousMillis;

  uint16_t servoDcMin;
  uint16_t servoDcMax;
};
