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
// Host simulator: runs the real Pitmaster (src/pitmaster) against a gas
// smoker model. Writes CSV traces and a summary to out/, exit code 1 if an
// acceptance criterion fails.
//
//   ./gas_sim [outdir]            all scenarios
//   ./gas_sim --plant file.json   (see tools/fit_plant.py) adds a fitted plant

#include "Arduino.h"
#include "ArduinoLog.h"
#include "Pitmaster.h"
#include "GasDefaults.h"
#include "plant.h"
#include "reference_pid.h"

#include <map>
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

// ---- stub globals ----
uint32_t gSimMillis = 0;
uint32_t gSimLedcDuty[SIM_MAX_CHANNELS] = {0};
int gSimPinChannel[SIM_MAX_PINS];
uint8_t gSimDac[SIM_MAX_PINS] = {0};
uint8_t gSimDigital[SIM_MAX_PINS] = {0};
SimSerial Serial;
SimLog Log;
bool gSimLogEnabled = false;

#define IO1 25u
#define CH1 0u
#define IO2 33u
#define CH2 1u

static std::string gOutDir = "out";
static uint32_t gSeedOffset = 0;
static int gFailures = 0;
static std::ostringstream gSummary;

static void resetIo()
{
  for (int i = 0; i < SIM_MAX_PINS; i++)
    gSimPinChannel[i] = -1;
  memset(gSimLedcDuty, 0, sizeof(gSimLedcDuty));
}

static void check(bool ok, const std::string &what)
{
  if (!ok)
  {
    gFailures++;
    std::cout << "  FAIL: " << what << std::endl;
  }
  else
  {
    std::cout << "  ok:   " << what << std::endl;
  }
}

static std::string fmt(double v, int digits = 1)
{
  std::ostringstream s;
  s << std::fixed << std::setprecision(digits) << v;
  return s.str();
}

static PlantParams makePlant(const char *name, double L, double tau, double tMin, double tMax, double sensorLag)
{
  PlantParams p;
  p.name = name;
  p.deadTimeS = L;
  p.tauS = tau;
  p.tAmb = 20;
  p.tMin = tMin;
  p.tMax = tMax;
  p.valveExp = 0.5;
  p.tauSensorS = sensorLag;
  p.noise = 0.3;
  p.tauLidS = 90;
  return p;
}

static PitmasterProfile makeGasProfile(float minOutput)
{
  //                       Name,       Nr, Aktor, Kp,  Ki,    Kd,  DCmin, DCmax, JP, SPMIN, SPMAX, LINK, OPL, TUNE, MINOUT, ATD, ATH, ATRULE, FO, FODROP, FOWIN
  PitmasterProfile p = {"Gas Servo", 4, SERVO, GAS_KP, GAS_KI, GAS_KD, 0, 100, 100, 0, 100, 0, 1, 0, minOutput, 20, 1.5, 0, 1, 15, 300};
  return p;
}

// ---------------------------------------------------------------------------
// simulation of one device with one pitmaster
// ---------------------------------------------------------------------------
class Sim
{
public:
  Sim(const PlantParams &params, PitmasterProfile prof, float setpoint, double startTemp, uint32_t seed)
      : plant(params, startTemp, seed), profile(prof)
  {
    resetIo();
    gSimMillis = 0;
    pm = new Pitmaster(IO1, CH1, IO2, CH2);
    pm->assignProfile(&profile);
    // fill the sensor filter before the pitmaster sees the channel
    for (int i = 0; i < 10; i++)
      temperature.update(plant.measure());
    temperature.refresh();
    pm->assignTemperature(&temperature);
    pm->setTargetTemperature(setpoint);
    opening = 0; // burner lit at the smallest stage
    events = 0;
    flameOutEventMs = 0;
    saves = 0;
    csv = NULL;
    csvStepMs = 5000u;
  }

  ~Sim()
  {
    if (csv)
      csv->close();
    delete csv;
    delete pm; // Pitmaster has no virtual destructor, but is not derived here
  }

  void openCsv(const std::string &name)
  {
    csv = new std::ofstream(name.find('/') != std::string::npos || name.find(".csv") != std::string::npos ? name : gOutDir + "/" + name + ".csv");
    *csv << "t_s,raw,temp,set,output,opening,p,i,d,esum,jump,lid,flame_susp,flame_out,at_phase,at_cycle,chamber,servo_us,at_state,type\n";
  }

  // servo position seen by the valve: the servo follows the PWM while it
  // is attached and holds its position when no pulses are sent
  void updateServo()
  {
    if (gSimPinChannel[IO2] == (int)CH2)
    {
      uint32_t duty = gSimLedcDuty[CH2];
      if (duty > 0u)
      {
        double pulseMin = (uint16_t)(pm->getServoMinDutyCyle() / 0.30518f);
        double pulseMax = (uint16_t)(pm->getServoMaxDutyCyle() / 0.30518f);
        double spMin = pulseMin + (pulseMax - pulseMin) * profile.spmin / 100.0;
        double spMax = pulseMin + (pulseMax - pulseMin) * profile.spmax / 100.0;
        opening = (duty - spMin) / (spMax - spMin) * 100.0;
        opening = std::max(0.0, std::min(100.0, opening));
      }
    }
  }

  void step()
  {
    updateServo();
    plant.step(0.2, opening);
    lastRaw = plant.measure();
    temperature.update(lastRaw);
    gSimMillis += 200;

    if ((gSimMillis % 1000u) == 0u)
    {
      temperature.refresh();
      pm->update();
      uint8_t ev = pm->takeEvents();
      events |= ev;
      if (ev & PM_EVENT_SAVE)
        saves++;
      if ((ev & PM_EVENT_FLAMEOUT) && (0u == flameOutEventMs))
        flameOutEventMs = gSimMillis;
      updateServo();

      if (csv && ((gSimMillis % csvStepMs) == 0u))
        writeCsv();
    }
  }

  void writeCsv()
  {
    AutoTuneRelay *at = pm->getAutoTune();
    PidCore *pid = pm->getPid();
    *csv << fmt(gSimMillis / 1000.0, 0) << "," << fmt(lastRaw, 2) << "," << fmt(temperature.getValue(), 2) << ","
         << fmt(pm->getTargetTemperature(), 1) << "," << fmt(pm->getValue(), 2) << "," << fmt(opening, 2) << ","
         << fmt(pid->getP(), 2) << "," << fmt(pid->getI(), 2) << "," << fmt(pid->getD(), 2) << "," << fmt(pid->getEsum(), 1) << ","
         << (pid->isJump() ? 1 : 0) << "," << (pm->getOPLStatus() ? 1 : 0) << "," << (pm->isFlameOutSuspected() ? 1 : 0) << ","
         << (pm->isFlameOut() ? 1 : 0) << "," << (at ? (int)at->getPhase() : 0) << "," << (at ? (int)at->getCycle() : 0) << ","
         << fmt(plant.getChamber(), 2) << "," << pm->getServoPulseUs() << "," << (at ? (int)at->getState() : 0) << ","
         << (int)pm->getType() << "\n";
  }

  void runFor(double seconds)
  {
    uint32_t steps = (uint32_t)(seconds * 5.0);
    for (uint32_t i = 0; i < steps; i++)
      step();
  }

  // run until cond() is true (checked every second) or timeout, returns true if cond met
  bool runUntil(std::function<bool()> cond, double maxSeconds)
  {
    uint32_t steps = (uint32_t)(maxSeconds * 5.0);
    for (uint32_t i = 0; i < steps; i++)
    {
      step();
      if ((gSimMillis % 1000u) == 0u && cond())
        return true;
    }
    return false;
  }

  double now() const { return gSimMillis / 1000.0; }
  bool servoAttached() const { return gSimPinChannel[IO2] == (int)CH2; }

  GasSmokerPlant plant;
  TemperatureBase temperature;
  PitmasterProfile profile;
  Pitmaster *pm;
  double opening;
  double lastRaw;
  uint8_t events;
  uint32_t flameOutEventMs;
  int saves;
  std::ofstream *csv;
  uint32_t csvStepMs;
};

// statistics on the device temperature over a time span
struct Span
{
  double maxDev = 0;   // max |x - set|
  double maxOver = -1e9; // max (x - set)
  double sumAbs = 0;
  int n = 0;
  double minOpening = 1e9;
  void add(double x, double set, double opening)
  {
    maxDev = std::max(maxDev, fabs(x - set));
    maxOver = std::max(maxOver, x - set);
    sumAbs += fabs(x - set);
    minOpening = std::min(minOpening, opening);
    n++;
  }
};

static Span observe(Sim &sim, double seconds)
{
  Span span;
  uint32_t steps = (uint32_t)(seconds * 5.0);
  for (uint32_t i = 0; i < steps; i++)
  {
    sim.step();
    if ((gSimMillis % 1000u) == 0u)
      span.add(sim.temperature.getValue(), sim.pm->getTargetTemperature(), sim.opening);
  }
  return span;
}

// ---------------------------------------------------------------------------
// test 0: PidCore computes exactly the same as the upstream pidCalc()
// ---------------------------------------------------------------------------
static void testPidEquivalence()
{
  std::cout << "PID equivalence (PidCore vs upstream pidCalc)" << std::endl;
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> temp(20, 220);
  int mismatches = 0;

  for (int run = 0; run < 20; run++)
  {
    PidCore core;
    RefPid ref;
    float kp = 1 + run, ki = (run % 3) * 0.01f + 0.001f * run, kd = run * 5.0f;
    float w = 80 + run * 5;
    float x = 25;
    bool ssr = (run % 4) == 0;
    for (int i = 0; i < 5000; i++)
    {
      x += (temp(rng) - x) * 0.01f;
      if (i == 2500)
        ki *= 2; // Ki change path
      float a = core.calc(x, w, kp, ki, kd, ssr, 100, 1000, true);
      float b = ref.calc(x, w, kp, ki, kd, ssr);
      if (a != b)
        mismatches++;
    }
  }
  check(0 == mismatches, "PidCore == upstream pidCalc (100000 samples, bit exact)");
}

// ---------------------------------------------------------------------------
// test 1: tuning rule and unit derivation
// ---------------------------------------------------------------------------
static void testRules()
{
  std::cout << "Tuning rules" << std::endl;
  float kp, ki, kd;
  AutoTuneRelay::computeGains(AtRule::TyreusLuybenPid, 10.0f, 300.0f, kp, ki, kd);
  check(fabs(kp - 10.0 / 2.2) < 1e-4 && fabs(ki - (10.0 / 2.2) / (2.2 * 300)) < 1e-6 && fabs(kd - (10.0 / 2.2) * 300 / 6.3) < 1e-3,
        "Tyreus-Luyben: Kp=Ku/2.2, ki=Kp/(2.2Tu), kd=Kp*Tu/6.3");
  AutoTuneRelay::computeGains(AtRule::ZieglerNicholsNoOvershootPid, 10.0f, 300.0f, kp, ki, kd);
  check(fabs(kp - 2.0) < 1e-4 && fabs(ki - 2.0 / 150) < 1e-6 && fabs(kd - 200.0) < 1e-3, "ZN no overshoot: Kp=0.2Ku, Ti=Tu/2, Td=Tu/3");
}

// ---------------------------------------------------------------------------
// main scenario: autotune from cold, hold, +20 step, lid, flame out
// ---------------------------------------------------------------------------
struct ScenarioResult
{
  std::string name;
  bool atDone = false;
  int atError = 0;
  double atTimeMin = 0;
  double relayTimeMin = 0;
  double bias = 0, tu = 0, a = 0, ku = 0, kp = 0, ki = 0, kd = 0;
  int cycles = 0;
  int warnings = 0;
  double handoverDev = 0, holdDev = 0, overshoot = 0, stepDev = 0;
  bool lidFalseAlarm = false;
  double flameDetectS = -1;
  double flameOpening = -1;
};

static std::vector<ScenarioResult> gResults;

static ScenarioResult runMainScenario(const PlantParams &plant, float setpoint, float minOutput, uint32_t seed,
                                      double maxAutotuneMin = 60.0)
{
  ScenarioResult r;
  r.name = plant.name + "_" + fmt(setpoint, 0);
  std::cout << "Scenario " << r.name << std::endl;

  Sim sim(plant, makeGasProfile(minOutput), setpoint, 20.0, seed);
  sim.openCsv("main_" + r.name);

  // pitmaster on auto, autotune started explicitly
  sim.runFor(2);
  bool started = sim.pm->startAutoTune();
  check(started, r.name + ": autotune started");

  double t0 = sim.now();
  double relayStart = -1;
  bool finished = sim.runUntil([&]() {
    AutoTuneRelay *at = sim.pm->getAutoTune();
    if (relayStart < 0 && at->getPhase() == AtPhase::Relay)
      relayStart = sim.now();
    return !at->isRunning(); }, 150 * 60);

  AutoTuneRelay *at = sim.pm->getAutoTune();
  r.atDone = finished && (at->getState() == AtState::Done);
  r.atError = (int)at->getError();
  r.atTimeMin = (sim.now() - t0) / 60.0;
  r.relayTimeMin = (relayStart > 0) ? (sim.now() - relayStart) / 60.0 : 0;
  r.bias = at->getBias();
  r.tu = at->getTu();
  r.a = at->getA();
  r.ku = at->getKu();
  r.kp = at->getKp();
  r.ki = at->getKi();
  r.kd = at->getKd();
  r.cycles = at->getCycle();
  r.warnings = at->getWarnings();

  check(r.atDone, r.name + ": autotune done (error " + std::to_string(r.atError) + ")");
  check(r.atTimeMin <= maxAutotuneMin, r.name + ": autotune converged within " + fmt(maxAutotuneMin, 0) + " min (" + fmt(r.atTimeMin) + " min)");
  check(sim.profile.kp == at->getKp() && sim.profile.ki == at->getKi(), r.name + ": gains written to profile");
  check(sim.saves > 0, r.name + ": save requested");

  if (!r.atDone)
  {
    gResults.push_back(r);
    return r;
  }

  // handover transient: first 30 min after the autotune, then hold 15 min
  Span handover = observe(sim, 30 * 60);
  r.handoverDev = handover.maxDev;
  Span hold = observe(sim, 15 * 60);
  r.holdDev = hold.maxDev;
  check(hold.maxDev <= 2.0, r.name + ": hold +-2 °C (max " + fmt(hold.maxDev, 2) + ")");

  // setpoint +20
  sim.pm->setTargetTemperature(setpoint + 20);
  Span step1 = observe(sim, 30 * 60);
  Span step2 = observe(sim, 15 * 60);
  r.overshoot = std::max(step1.maxOver, step2.maxOver);
  r.stepDev = step2.maxDev;
  check(r.overshoot < 5.0, r.name + ": overshoot after +20 step < 5 °C (" + fmt(r.overshoot, 2) + ")");
  check(step2.maxDev <= 2.0, r.name + ": steady +-2 °C after step (max " + fmt(step2.maxDev, 2) + ")");
  check(std::min(hold.minOpening, step1.minOpening) >= minOutput - 0.5, r.name + ": opening never below minOutput");

  // lid open for 60 s, then 30 min: no flame out alarm
  sim.plant.lidOpen = true;
  sim.runFor(60);
  sim.plant.lidOpen = false;
  sim.runFor(30 * 60);
  r.lidFalseAlarm = sim.pm->isFlameOut();
  check(!r.lidFalseAlarm, r.name + ": lid open does not trigger flame out");

  // flame out
  double tFlame = sim.now();
  sim.plant.flameOut = true;
  sim.runUntil([&]() { return sim.pm->isFlameOut(); }, 15 * 60);
  if (sim.flameOutEventMs > 0)
    r.flameDetectS = sim.flameOutEventMs / 1000.0 - tFlame;
  check(sim.pm->isFlameOut() && r.flameDetectS <= 360, r.name + ": flame out detected within 6 min (" + fmt(r.flameDetectS, 0) + " s)");
  sim.runFor(5);
  r.flameOpening = sim.opening;
  check(fabs(sim.opening - minOutput) < 0.6, r.name + ": flame out -> opening at minOutput (" + fmt(sim.opening, 1) + ")");
  sim.runFor(10 * 60);
  check(fabs(sim.opening - minOutput) < 0.6 && sim.pm->isFlameOut(), r.name + ": stays at minOutput until acknowledged");

  gResults.push_back(r);
  return r;
}

// ---------------------------------------------------------------------------
// abort paths: servo must end at minOutput, then be detached
// ---------------------------------------------------------------------------
static void runAbortScenario(const std::string &name, std::function<void(Sim &)> inject, AtError expected,
                             bool expectDetach = true)
{
  const float minOutput = 12.0f;
  PlantParams plant = makePlant("P2", 40, 420, 75, 250, 20);
  Sim sim(plant, makeGasProfile(minOutput), 150, 20.0, 7);
  sim.openCsv("abort_" + name);
  std::cout << "Abort " << name << std::endl;

  sim.runFor(2);
  sim.pm->startAutoTune();
  // into the relay phase, at the high output
  bool relay = sim.runUntil([&]() {
    AutoTuneRelay *at = sim.pm->getAutoTune();
    return at->getPhase() == AtPhase::Relay && sim.pm->getValue() > 30; }, 90 * 60);
  check(relay, name + ": relay phase with high output reached");
  sim.runFor(5);
  double before = sim.opening;

  inject(sim);
  // run until the abort happened (immediately for direct calls)
  sim.runUntil([&]() { return !sim.pm->isAutoTuneRunning(); }, 60 * 60);
  AutoTuneRelay *at = sim.pm->getAutoTune();
  check(at->getState() == AtState::Aborted && at->getError() == expected,
        name + ": aborted with error " + std::to_string((int)expected) + " (got " + std::to_string((int)at->getError()) + ")");
  check(fabs(sim.opening - minOutput) < 0.6, name + ": servo at minOutput (" + fmt(before, 1) + " -> " + fmt(sim.opening, 1) + ")");
  check(sim.servoAttached(), name + ": servo still powered while moving");
  sim.runFor(4);
  if (expectDetach)
    check(!sim.servoAttached(), name + ": servo detached after the movement");
  check(fabs(sim.opening - minOutput) < 0.6, name + ": servo stays at minOutput (" + fmt(sim.opening, 1) + ")");
  check(sim.pm->getType() == pm_off, name + ": pitmaster off");
  sim.runFor(60);
  check(fabs(sim.opening - minOutput) < 0.6, name + ": after 60 s still at minOutput");
}

static void runAbortScenarios()
{
  runAbortScenario("sensor_lost", [](Sim &s) { s.plant.sensorLost = true; }, AtError::SensorLost);
  runAbortScenario("overtemp", [](Sim &s) { s.plant.extraHeat = 80; }, AtError::OverTemp);
  runAbortScenario("user_stop", [](Sim &s) { s.pm->stopAutoTune(AtError::UserStop); }, AtError::UserStop);
  runAbortScenario("type_off", [](Sim &s) { s.pm->setType(pm_off); }, AtError::UserStop);
  runAbortScenario("setpoint_change", [](Sim &s) { s.pm->setTargetTemperature(160); }, AtError::SetpointChange);
  static PitmasterProfile other = makeGasProfile(12.0f);
  other.id = 2;
  runAbortScenario("profile_change", [](Sim &s) { s.pm->assignProfile(&other); s.pm->assignProfile(&s.profile); }, AtError::ProfileChange);
  runAbortScenario("flame_out", [](Sim &s) { s.plant.flameOut = true; }, AtError::FlameOut);
}

// device restart during autotune: servo position unknown, must go to minOutput
static void runRestartScenario()
{
  std::cout << "Abort restart" << std::endl;
  const float minOutput = 12.0f;
  PlantParams plant = makePlant("P2", 40, 420, 75, 250, 20);
  Sim sim(plant, makeGasProfile(minOutput), 150, 20.0, 9);
  sim.runFor(2);
  sim.pm->startAutoTune();
  sim.runUntil([&]() { return sim.pm->getAutoTune()->getPhase() == AtPhase::Relay && sim.pm->getValue() > 30; }, 90 * 60);
  double before = sim.opening;

  // restart: PWM stops (servo holds), new firmware instance, config says "at":1
  resetIo();
  delete sim.pm;
  sim.pm = new Pitmaster(IO1, CH1, IO2, CH2);
  sim.pm->assignProfile(&sim.profile);
  sim.pm->assignTemperature(&sim.temperature);
  sim.pm->setTargetTemperature(150);
  sim.pm->setType(pm_auto);           // saved type
  sim.pm->markAutoTuneRestartAbort(); // saved "at":1
  sim.runFor(3);
  check(fabs(before - minOutput) > 5, "restart: servo was away from minOutput before (" + fmt(before, 1) + ")");
  check(fabs(sim.opening - minOutput) < 0.6, "restart: servo driven to minOutput (" + fmt(sim.opening, 1) + ")");
  check(sim.pm->getType() == pm_manual, "restart: pitmaster holds minOutput in manual mode");
  check(sim.pm->getAutoTune()->getError() == AtError::Restart, "restart: status shows restart abort");
  sim.runFor(600);
  check(fabs(sim.opening - minOutput) < 0.6, "restart: still at minOutput after 10 min");
}

// security review F-6 / package S1: a restart (watchdog, crash, power
// glitch) with the saved type "off" must not leave the servo where it was
static void runRestartWithTypeOff()
{
  std::cout << "Restart with type off" << std::endl;
  const float minOutput = 10.0f;
  PlantParams plant = makePlant("P2", 40, 420, 75, 250, 20);
  Sim sim(plant, makeGasProfile(minOutput), 150, 20.0, 13);
  sim.pm->setType(pm_manual);
  sim.pm->setValue(80);
  sim.runFor(60);
  double before = sim.opening;
  // restart: PWM stops, servo holds; saved config: type off
  resetIo();
  delete sim.pm;
  sim.pm = new Pitmaster(IO1, CH1, IO2, CH2);
  sim.pm->assignProfile(&sim.profile);
  sim.pm->assignTemperature(&sim.temperature);
  sim.pm->setTargetTemperature(150);
  sim.pm->setType(pm_off);
  sim.runFor(1);
  check(before > 70 && fabs(sim.opening - minOutput) < 0.6 && sim.servoAttached(),
        "restart off: servo driven to minOutput (" + fmt(before, 1) + " -> " + fmt(sim.opening, 1) + ")");
  sim.runFor(4);
  check(!sim.servoAttached() && fabs(sim.opening - minOutput) < 0.6, "restart off: then detached at minOutput");
  sim.runFor(600);
  check(fabs(sim.opening - minOutput) < 0.6, "restart off: still at minOutput after 10 min");

  // restart with saved auto: boot park first, then the PID takes over
  resetIo();
  delete sim.pm;
  sim.pm = new Pitmaster(IO1, CH1, IO2, CH2);
  sim.pm->assignProfile(&sim.profile);
  sim.pm->assignTemperature(&sim.temperature);
  sim.pm->setTargetTemperature(150);
  sim.pm->setType(pm_auto);
  sim.runFor(1);
  check(fabs(sim.opening - minOutput) < 0.6, "restart auto: first minOutput");
  sim.runFor(120);
  check(sim.servoAttached() && sim.opening > minOutput + 5, "restart auto: then PID controls (" + fmt(sim.opening, 1) + ")");
}

// pitmaster off (also outside autotune) parks the servo at minOutput
static void runOffScenario()
{
  std::cout << "Pitmaster off" << std::endl;
  const float minOutput = 15.0f;
  PlantParams plant = makePlant("P1", 20, 240, 70, 220, 10);
  Sim sim(plant, makeGasProfile(minOutput), 150, 20.0, 11);
  sim.pm->setType(pm_auto);
  sim.runFor(60);
  check(sim.opening > 50, "off: heating with high output (" + fmt(sim.opening, 1) + ")");
  sim.pm->setType(pm_off);
  sim.runFor(1);
  check(fabs(sim.opening - minOutput) < 0.6 && sim.servoAttached(), "off: servo moves to minOutput first");
  sim.runFor(4);
  check(fabs(sim.opening - minOutput) < 0.6 && !sim.servoAttached(), "off: then detached at minOutput");

  // lid open in auto keeps minOutput as lower limit
  sim.pm->setType(pm_auto);
  sim.runFor(20 * 60);
  double minOpen = 100;
  sim.plant.lidOpen = true;
  for (int i = 0; i < 120 * 5; i++)
  {
    sim.step();
    minOpen = std::min(minOpen, sim.opening);
  }
  sim.plant.lidOpen = false;
  check(minOpen >= minOutput - 0.5, "lid open: opening not below minOutput (" + fmt(minOpen, 1) + ")");
}

// long PID run without disturbances: no false flame out
static void runNoFalseAlarm(const PlantParams &plant, float setpoint)
{
  std::cout << "No false alarm " << plant.name << " " << setpoint << std::endl;
  Sim sim(plant, makeGasProfile(0), setpoint, 20.0, 21);
  sim.pm->setType(pm_auto);
  sim.runFor(4 * 3600);
  check(!sim.pm->isFlameOut(), plant.name + "_" + fmt(setpoint, 0) + ": 4 h PID with defaults, no false flame out");
  // setpoint lowered by 30 °C: temperature falls, but above setpoint -> no alarm
  sim.pm->setTargetTemperature(setpoint - 30);
  sim.runFor(3600);
  check(!sim.pm->isFlameOut(), plant.name + "_" + fmt(setpoint, 0) + ": setpoint -30 °C, no false flame out");
}

// PID only with given gains: cold start, hold, +20 step (no autotune)
struct PidOnlyResult
{
  double coldOver, holdDev, stepOver, stepDev, minOpening;
};

static PidOnlyResult runPidOnly(const PlantParams &plant, float setpoint, float kp, float ki, float kd, uint32_t seed, const std::string &csvName = "")
{
  PitmasterProfile prof = makeGasProfile(0);
  prof.kp = kp;
  prof.ki = ki;
  prof.kd = kd;
  Sim sim(plant, prof, setpoint, 20.0, seed);
  if (!csvName.empty())
    sim.openCsv(csvName);
  sim.pm->setType(pm_auto);
  PidOnlyResult r;
  Span cold = observe(sim, 60 * 60);
  Span hold = observe(sim, 15 * 60);
  sim.pm->setTargetTemperature(setpoint + 20);
  Span step1 = observe(sim, 45 * 60);
  Span step2 = observe(sim, 15 * 60);
  r.coldOver = cold.maxOver;
  r.holdDev = hold.maxDev;
  r.stepOver = std::max(step1.maxOver, step2.maxOver);
  r.stepDev = step2.maxDev;
  r.minOpening = cold.minOpening;
  return r;
}

// a typical cook evening on a given plant, written as 1 s CSV for the
// logger and analysis tools (tools/sim_to_logger.py, analyze_cook.py)
static void runCook(const PlantParams &plant, const std::string &csvName, bool autotune)
{
  gOutDir = ".";
  Sim sim(plant, makeGasProfile(2.0f), 130, 20.0, 4242);
  sim.csvStepMs = 1000u;
  sim.openCsv(csvName);
  sim.pm->setType(pm_auto);
  if (autotune)
  {
    sim.runFor(2);
    sim.pm->startAutoTune();
    sim.runUntil([&]() { return !sim.pm->isAutoTuneRunning(); }, 120 * 60);
  }
  sim.runFor(60 * 60);
  sim.plant.lidOpen = true; // spritzing
  sim.runFor(45);
  sim.plant.lidOpen = false;
  sim.runFor(45 * 60);
  sim.pm->setTargetTemperature(150);
  sim.runFor(45 * 60);
  sim.plant.lidOpen = true; // wrapping
  sim.runFor(90);
  sim.plant.lidOpen = false;
  sim.runFor(40 * 60);
  sim.plant.flameOut = true; // gas bottle empty
  sim.runFor(15 * 60);
}

int main(int argc, char **argv)
{
  if (argc >= 10 && std::string(argv[1]) == "--cook")
  {
    // --cook out.csv L tau tMin tMax sensorLag valveExp [autotune 0/1]
    PlantParams p = makePlant("cook", atof(argv[3]), atof(argv[4]), atof(argv[5]), atof(argv[6]), atof(argv[7]));
    p.valveExp = atof(argv[8]);
    runCook(p, argv[2], atoi(argv[9]) != 0);
    return 0;
  }

  if (argc == 5 && std::string(argv[1]) == "--defaults")
  {
    float kp = atof(argv[2]), ki = atof(argv[3]), kd = atof(argv[4]);
    std::vector<PlantParams> ps = {makePlant("P1", 20, 240, 70, 220, 10), makePlant("P2", 40, 420, 75, 250, 20), makePlant("P3", 60, 600, 80, 280, 30)};
    for (float set : {110.0f, 150.0f})
      for (auto &p : ps)
      {
        PidOnlyResult r = runPidOnly(p, set, kp, ki, kd, 5);
        std::cout << p.name << "_" << set << " coldOver " << fmt(r.coldOver, 2) << " hold " << fmt(r.holdDev, 2) << " stepOver " << fmt(r.stepOver, 2)
                  << " stepDev " << fmt(r.stepDev, 2) << std::endl;
      }
    return 0;
  }

  std::vector<PlantParams> plants = {
      makePlant("P1", 20, 240, 70, 220, 10),
      makePlant("P2", 40, 420, 75, 250, 20),
      makePlant("P3", 60, 600, 80, 280, 30),
  };

  for (int i = 1; i < argc; i++)
  {
    std::string arg = argv[i];
    if (arg == "--plant" && (i + 1) < argc)
    {
      // fitted plant from tools/fit_plant.py: name L tau tMin tMax sensorLag valveExp
      std::ifstream f(argv[++i]);
      PlantParams p;
      f >> p.name >> p.deadTimeS >> p.tauS >> p.tMin >> p.tMax >> p.tauSensorS >> p.valveExp;
      p.tAmb = 20;
      p.noise = 0.3;
      p.tauLidS = 90;
      if (f)
        plants.push_back(p);
      else
        std::cerr << "cannot read plant file" << std::endl;
    }
    else if (arg == "--seed-offset" && (i + 1) < argc)
    {
      gSeedOffset = (uint32_t)atoi(argv[++i]);
    }
    else if (arg == "-v")
    {
      gSimLogEnabled = true;
    }
    else
    {
      gOutDir = arg;
    }
  }

  testPidEquivalence();
  testRules();

  for (float set : {110.0f, 150.0f})
    for (size_t i = 0; i < plants.size(); i++)
      runMainScenario(plants[i], set, 0.0f, 1000 + i + gSeedOffset * 97u);

  // one run with minOutput (wind protection)
  // additional: minOutput 8 % (wind protection). The low relay output is
  // close to the setpoint here, cycles get long; only the 90 min timeout applies.
  runMainScenario(plants[1], 130, 8.0f, 77 + gSeedOffset * 97u, 90.0);

  runAbortScenarios();
  runRestartScenario();
  runRestartWithTypeOff();
  runOffScenario();
  for (size_t i = 0; i < plants.size(); i++)
    runNoFalseAlarm(plants[i], 130);

  // summary table
  std::ofstream md(gOutDir + "/summary.md");
  md << "| Szenario | AT ok | AT Zeit [min] | davon Relay [min] | Zyklen | Bias [%] | Tu [s] | a [°C] | Ku | Kp | Ki | Kd | Übergabe max [°C] | Halten max [°C] | Überschwingen +20 [°C] | nach Sprung max [°C] | Deckel Fehlalarm | Flamme aus erkannt [s] |\n";
  md << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  for (auto &r : gResults)
  {
    md << "| " << r.name << " | " << (r.atDone ? "ja" : "nein (" + std::to_string(r.atError) + ")") << " | " << fmt(r.atTimeMin) << " | "
       << fmt(r.relayTimeMin) << " | " << r.cycles << (r.warnings ? "*" : "") << " | " << fmt(r.bias) << " | " << fmt(r.tu, 0) << " | " << fmt(r.a, 2) << " | "
       << fmt(r.ku, 2) << " | " << fmt(r.kp, 2) << " | " << fmt(r.ki, 4) << " | " << fmt(r.kd, 1) << " | " << fmt(r.handoverDev, 2) << " | " << fmt(r.holdDev, 2) << " | "
       << fmt(r.overshoot, 2) << " | " << fmt(r.stepDev, 2) << " | " << (r.lidFalseAlarm ? "JA" : "nein") << " | " << fmt(r.flameDetectS, 0) << " |\n";
  }
  md.close();

  std::cout << std::endl
            << (gFailures ? "FAILED: " : "ALL PASSED") << (gFailures ? std::to_string(gFailures) : "") << std::endl;
  return gFailures ? 1 : 0;
}
