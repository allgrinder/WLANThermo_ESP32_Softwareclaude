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
    
    HISTORY: Please refer Github History
    
****************************************************/
#include "Pitmaster.h"
#include "DbgPrint.h"
#include "math.h"
#include "ArduinoLog.h"

#define PITMIN 0    // LOWER LIMIT SET
#define PITMAX 100  // UPPER LIMIT SET
#define PITMASTERSETMIN 50
#define PITMASTERSETMAX 200

#define SSR_FREQUENCY 0.5
#define SSR_BIT_RES 16
#define SERVO_FREQUENCY 50
#define SERVO_BIT_RES 16

#define PAUSE_DEFAULT 1000u
#define PAUSE_SSR 2000u
#define FAN_BOOST_VALUE 30u

#define OPL_FALL 97   // OPEN LID LIMIT FALLING
#define OPL_RISE 100  // OPEN LID LIMIT RISING
#define OPL_PAUSE 300 // OPEN LID PAUSE
#define OPL_PAUSE_FLAMEOUT 120 // OPEN LID PAUSE with flame out detection (gas)
#define OPL_THRESHOLD 2

#define MEDIAN_SIZE 10u
#define PM_DEFAULT_DCOUNT 15u
#define PM_DEFAULT_DCOUNT_MIN 1u
#define PM_DEFAULT_DCOUNT_MAX 60u

#define JP_THRESHOLD_CELSIUS 100u
#define JP_THRESHOLD_FAHRENHEIT 212u

// duty cycle calculation
// frequency: 50Hz --> 20ms
// timer resolution: 16 bit --> 65535
// resolution: 20.000us / 65535 --> 0,305us
// e.g. servo min duty cyle:  550us / 0,305...us --> 1802
// e.g. servo min duty cyle: 2250us / 0,305...us --> 7373
#define PM_SERVO_DUTY_CYCLE_DIVISOR 0.30518f
#define PM_DEFAULT_SERVO_MIN_DUTY_CYCLE 550u    //in uS
#define PM_DEFAULT_SERVO_MAX_DUTY_CYCLE 2250u   //in uS
#define PM_CONFIG_SERVO_MIN_DUTY_CYCLE 400u     //in uS
#define PM_CONFIG_SERVO_MAX_DUTY_CYCLE 2600     //in uS

uint8_t Pitmaster::ioSupply = PITMASTER_NO_SUPPLY_IO;
uint8_t Pitmaster::ioSupplyRequested = 0u;
uint8_t Pitmaster::globalIndexTracker = 0u;

Pitmaster::Pitmaster(uint8_t ioPin1, uint8_t channel1, uint8_t ioPin2, uint8_t channel2)
{
    this->profile = NULL;
    this->temperature = NULL;
    this->dutyCycleTest = NULL;
    this->autoTune = NULL;
    this->targetTemperature = PITMASTERSETMIN;
    this->type = pm_off;
    this->typeLast = pm_auto;
    this->value = 0u;
    this->previousMillis = 0u;
    this->ioPin1 = ioPin1;
    this->ioPin2 = ioPin2;
    this->channel1 = channel1;
    this->channel2 = channel2;
    this->initActuator = NOAR;
    this->globalIndex = this->globalIndexTracker++;
    this->registeredCb = NULL;
    this->settingsChanged = false;
    this->registeredCbUserData = NULL;
    this->cbValue = 0u;
    this->medianValue = new MedianFilter<float>(MEDIAN_SIZE);
    this->pid.setDCount(PM_DEFAULT_DCOUNT);
    this->filteredTemp = 0;
    this->medianPrimed = false;
    this->parkPending = false;
    this->parkUntil = 0u;
    this->events = 0u;

    this->servoDcMin = PM_DEFAULT_SERVO_MIN_DUTY_CYCLE;
    this->servoDcMax = PM_DEFAULT_SERVO_MAX_DUTY_CYCLE;

    memset((void *)&this->openLid, 0u, sizeof(this->openLid));

    pinMode(this->ioPin1, OUTPUT);
    digitalWrite(this->ioPin1, LOW);

    pinMode(this->ioPin2, OUTPUT);
    digitalWrite(this->ioPin2, LOW);
}

void Pitmaster::setType(PitmasterType type)
{
    // any change away from auto stops a running autotune
    if ((pm_auto != type) && this->isAutoTuneRunning())
    {
        this->abortAutoTune(AtError::UserStop);
    }

    if(pm_off != type)
    {
        this->typeLast  = type;
    }

    this->type = type;
    settingsChanged = true;
}

PitmasterType Pitmaster::getType()
{
    return this->type;
}

void Pitmaster::setTypeLast(PitmasterType type)
{
    this->typeLast = type;
    settingsChanged = true;
}

PitmasterType Pitmaster::getTypeLast()
{
    return this->typeLast;
}

void Pitmaster::assignProfile(PitmasterProfile *profile)
{
    if (profile != this->profile)
    {
        // abort before switching, parking uses the limits of the old profile
        if (this->isAutoTuneRunning())
            this->abortAutoTune(AtError::ProfileChange);

        this->pidReset();
        this->profile = profile;
        settingsChanged = true;
    }
}

PitmasterProfile *Pitmaster::getAssignedProfile()
{
    return this->profile;
}

void Pitmaster::assignTemperature(TemperatureBase *temperature)
{
    // Skip BLE and Maverick Radio temperatures for assignment
    /*   if ((temperature->getType() != (uint8_t)SensorType::Ble) &&
        (temperature->getType() != (uint8_t)SensorType::MaverickRadio))*/
      if (temperature->getType() != (uint8_t)SensorType::MaverickRadio)
    {
        if ((temperature != this->temperature) && this->isAutoTuneRunning())
            this->abortAutoTune(AtError::ChannelChange);

        this->temperature = temperature;
        this->medianPrimed = false;
        memset((void *)&this->openLid, 0u, sizeof(this->openLid));
        settingsChanged = true;
    }
}

TemperatureBase *Pitmaster::getAssignedTemperature()
{
    return this->temperature;
}

float Pitmaster::getValue()
{
    return this->value;
}

void Pitmaster::setValue(float value)
{
    this->value = value;
}

void Pitmaster::setTargetTemperature(float temperature)
{
    if (this->isAutoTuneRunning() && (fabs(temperature - this->autoTune->getSetpoint()) > 0.05f))
        this->abortAutoTune(AtError::SetpointChange);

    this->targetTemperature = temperature;
    settingsChanged = true;
}

float Pitmaster::getTargetTemperature()
{
    return this->targetTemperature;
}

void Pitmaster::setServoMinDutyCyle(uint16_t dutyCycle)
{
    // duty cycle is inside limit?
    if((dutyCycle >= PM_CONFIG_SERVO_MIN_DUTY_CYCLE) && (dutyCycle <= PM_CONFIG_SERVO_MAX_DUTY_CYCLE))
    {
        this->servoDcMin = dutyCycle;
    }
    else
    {
        Log.error("Pitmaster::setServoMinDutyCyle: dutyCycle = %d out of range!" CR, dutyCycle);
    }
}

void Pitmaster::setServoMaxDutyCyle(uint16_t dutyCycle)
{
    // duty cycle is inside limit?
    if((dutyCycle >= PM_CONFIG_SERVO_MIN_DUTY_CYCLE) && (dutyCycle <= PM_CONFIG_SERVO_MAX_DUTY_CYCLE))
    {
        this->servoDcMax = dutyCycle;
    }
    else
    {
        Log.error("Pitmaster::setServoMaxDutyCyle: dutyCycle = %d out of range!" CR, dutyCycle);
    }
}

void Pitmaster::setDCount(uint8_t dCount)
{
    // dCount is inside limit?
    if((dCount >= PM_DEFAULT_DCOUNT_MIN) && (dCount <= PM_DEFAULT_DCOUNT_MAX))
    {
        this->pid.setDCount(dCount);
    }
    else
    {
        Log.error("Pitmaster::setDCount: dCount = %d out of range!" CR, dCount);
    }
}

void Pitmaster::registerCallback(PitmasterCallback_t callback, void *userData)
{
    this->registeredCb = callback;
    this->registeredCbUserData = userData;
}

void Pitmaster::unregisterCallback()
{
    this->registeredCb = NULL;
}

void Pitmaster::handleCallbacks()
{
    if (this->registeredCb != NULL)
    {
        if (true == settingsChanged)
        {
            this->registeredCb(this, settingsChanged, this->registeredCbUserData);
            settingsChanged = false;
        }
        else if (cbValue != value)
        {
            this->registeredCb(this, settingsChanged, this->registeredCbUserData);
            cbValue = this->value;
        }
    }
}

boolean Pitmaster::checkPause()
{
    boolean pauseDone = false;
    uint currentMillis = millis();

    // Global Pitmaster Aktor from PID-Profil
    uint8_t actuator = this->profile->actuator;

    // overwrite, if DUTYCYCLE-Test
    if (this->dutyCycleTest != NULL)
    {
        actuator = this->dutyCycleTest->actuator;
    }

    switch (actuator)
    {
    case SSR:
        this->pause = 2000u;
        break;
    default:
        this->pause = 1000u;
        break;
    }

    if (currentMillis - this->previousMillis >= this->pause)
    {
        this->previousMillis = currentMillis;
        pauseDone = true;
    }

    return pauseDone;
}

boolean Pitmaster::startDutyCycleTest(uint8_t actuator, uint8_t value)
{
    if (this->dutyCycleTest != NULL)
        return false;

    this->dutyCycleTest = new DutyCycleTest();

    this->dutyCycleTest->actuator = actuator;
    this->dutyCycleTest->value = value;
    this->dutyCycleTest->timer = millis();

    return true;
}

float Pitmaster::getMinOutput()
{
    float minOutput = (this->profile != NULL) ? this->profile->minOutput : 0.0f;
    return constrain(minOutput, 0.0f, (float)PITMAX);
}

boolean Pitmaster::startAutoTune()
{
    if ((NULL == this->profile) || (NULL == this->temperature) || (false == this->temperature->isActive()))
        return false;

    if (this->isDutyCycleTestRunning())
        return false;

    if (NULL == this->autoTune)
    {
        this->autoTune = new AutoTuneRelay();
        if (NULL == this->autoTune)
            return false;
    }

    AtConfig config;
    AutoTuneRelay::defaultConfig(config);
    config.setpoint = this->targetTemperature;
    config.minOutput = this->getMinOutput();
    if (this->profile->atD > 0.0f)
        config.d = constrain(this->profile->atD, 5.0f, 50.0f);
    if (this->profile->atH > 0.0f)
        config.h = constrain(this->profile->atH, 0.2f, 10.0f);
    config.rule = (AtRule)this->profile->atRule;

    // autotune runs in auto mode, jump logic is disabled while it runs
    this->setType(pm_auto);
    this->pid.clearJump();
    this->flameOutDetector.reset();
    this->medianPrimed = false;
    this->autoTune->start(config, millis());
    this->profile->autotune = false;
    this->events |= PM_EVENT_SAVE;

    Log.notice("Autotune started: set %F, d %F, h %F, rule %d" CR, config.setpoint, config.d, config.h, (int)config.rule);

    return true;
}

void Pitmaster::stopAutoTune(AtError reason)
{
    if (this->isAutoTuneRunning())
        this->abortAutoTune(reason);
}

void Pitmaster::markAutoTuneRestartAbort()
{
    // device was restarted while an autotune was running: do not resume the
    // PID with unknown parameters, hold the burner at minOutput instead
    if (NULL == this->autoTune)
        this->autoTune = new AutoTuneRelay();

    if (this->autoTune != NULL)
        this->autoTune->markRestartAbort(this->targetTemperature);

    this->type = pm_manual;
    this->typeLast = pm_auto;
    this->value = this->getMinOutput();
    this->settingsChanged = true;
    this->events |= PM_EVENT_SAVE | PM_EVENT_AT_ABORTED;
    Log.error("Autotune interrupted by restart, holding minOutput" CR);
}

void Pitmaster::abortAutoTune(AtError reason)
{
    if (false == this->isAutoTuneRunning())
        return;

    this->autoTune->abort(reason);

    // abort: pitmaster off, servo first to minOutput, detached later
    this->type = pm_off;
    this->settingsChanged = true;
    this->value = this->getMinOutput();
    this->disableActuators(false);
    this->events |= PM_EVENT_SAVE | PM_EVENT_AT_ABORTED;

    Log.error("Autotune aborted, reason %d" CR, (int)reason);
}

void Pitmaster::acknowledgeFlameOut()
{
    if (this->flameOutDetector.isTriggered())
    {
        this->flameOutDetector.acknowledge();
        this->pid.reset();
        this->settingsChanged = true;
        Log.notice("Flame out acknowledged" CR);
    }
}

uint16_t Pitmaster::getServoPulseUs()
{
    if ((SERVO != initActuator) && (DAMPER != initActuator))
        return 0u;

    return (uint16_t)(ledcRead(this->channel2) * PM_SERVO_DUTY_CYCLE_DIVISOR);
}

uint8_t Pitmaster::takeEvents()
{
    uint8_t currentEvents = this->events;
    this->events = 0u;
    return currentEvents;
}

boolean Pitmaster::checkDutyCycleTest()
{
    boolean testDone = true;
    uint32_t ms;
    float newValue;

    if (this->dutyCycleTest != NULL)
    {
        testDone = false;
        ms = millis();

        if ((ms - this->dutyCycleTest->timer) > 10000u)
        {
            //disableActuators(); // stop test actuator
            delete (this->dutyCycleTest);
            this->dutyCycleTest = NULL;
            testDone = true;
        }
        else
        {
            newValue = this->dutyCycleTest->value;

            // Startanlauf: bei Servo beide male zuerst in die Mitte, bei Fan nur unten
            if (ms - dutyCycleTest->timer < 1000u)
            {
                if ((this->dutyCycleTest->actuator == FAN) || (this->dutyCycleTest->actuator == SERVO))
                    newValue = 50;
            }

            switch (this->dutyCycleTest->actuator)
            {
            case FAN:
                this->controlFan(newValue, 0, 100);
                break;
            case SERVO:
                this->controlServo(newValue, 0, 100);
                break;
            case SSR:
                this->controlSSR(newValue, 0, 100);
                break;
            default:
                break;
            }
        }
    }

    return testDone;
}

boolean Pitmaster::checkAutoTune()
{
    if ((NULL == this->autoTune) || (false == this->autoTune->isRunning()))
        return true;

    boolean active = this->temperature->isActive();
    float x = active ? this->filterTemperature() : (float)INACTIVEVALUE;
    float pidOutput = 0;

    // approach phase: regular PID without jump logic
    if (active && this->autoTune->needsPid())
        pidOutput = this->pidCalcFiltered(x, false);

    float newValue = this->autoTune->update(x, active, pidOutput, millis());

    if (AtState::Done == this->autoTune->getState())
    {
        this->profile->kp = this->autoTune->getKp();
        this->profile->ki = this->autoTune->getKi();
        this->profile->kd = this->autoTune->getKd();
        this->pid.reset();
        this->pid.preloadIntegral(this->autoTune->getOutputMean(), this->profile->ki);
        this->flameOutDetector.setBias(this->autoTune->getOutputMean());
        this->type = pm_auto;
        this->settingsChanged = true;
        this->events |= PM_EVENT_SAVE | PM_EVENT_AT_FINISHED;
        Log.notice("Autotune finished: Kp %F Ki %F Kd %F" CR, this->profile->kp, this->profile->ki, this->profile->kd);

        // continue in auto mode, start from the bias
        this->value = max(newValue, this->getMinOutput());
        this->controlActuators();
        return false;
    }

    if (AtState::Aborted == this->autoTune->getState())
    {
        // aborted inside the autotune (overtemp, timeout, sensor, ...)
        this->type = pm_off;
        this->settingsChanged = true;
        this->value = this->getMinOutput();
        this->disableActuators(false);
        this->events |= PM_EVENT_SAVE | PM_EVENT_AT_ABORTED;
        Log.error("Autotune aborted, reason %d" CR, (int)this->autoTune->getError());
        return false;
    }

    // flame out also during autotune
    if (this->profile->flameOut)
    {
        this->flameOutDetector.configure(this->profile->foDrop, this->profile->foWindow);
        bool relay = (AtPhase::Relay == this->autoTune->getPhase());
        if (this->flameOutDetector.update(x, this->targetTemperature, newValue, false, millis(), relay))
        {
            this->events |= PM_EVENT_FLAMEOUT;
            this->abortAutoTune(AtError::FlameOut);
            return false;
        }
    }

    this->value = newValue;
    this->controlActuators();

    return false;
}

boolean Pitmaster::checkOpenLid()
{
    if ((pm_auto == this->type) && (true == this->profile->opl) && true == this->temperature->isActive())
    {
        if (this->temperature->getGradient() == -1)
            openLid.fall_c++;
        else
            openLid.fall_c = 0;

        // erkennen ob Temperatur wieder eingependelt oder Timeout
        if (openLid.detected)
        { // Open Lid Detected

            openLid.count--;
            openLid.fall_c = 0;

            // extremes Überschwingen vermeiden
            if (openLid.temp > this->targetTemperature && this->temperature->getValue() < this->targetTemperature)
            {    
                openLid.temp = this->targetTemperature;
                Log.notice("OPL Reference: %F" CR, (float)(openLid.temp));
            }

            if (openLid.count <= 0) // Timeout
            {
                openLid.detected = false;
                Log.notice("OPL finished: Timeout" CR);
            }    

            else if (this->temperature->getValue() > (openLid.temp * (OPL_RISE / 100.0))) // Lid Closed
            {    
                openLid.detected = false;
                Log.notice("OPL finished: Lid closed" CR);
            }
        }
        else if (openLid.fall_c == 1)
        {
            openLid.ref = (this->temperature->getPreValue() == INACTIVEVALUE) ? this->temperature->getValue() : this->temperature->getPreValue();
        }
        else if (openLid.fall_c == 2)
        {
            //Log.notice("OPL 0: %F" CR, (float)(openLid.ref));
            //Log.notice("OPL 1: %F" CR, (float)(this->temperature->getPreValue()));
            //Log.notice("OPL 2: %F" CR, (float)(this->temperature->getValue()));
        }
        else if (openLid.fall_c > 10) {
            openLid.fall_c = 0;  // Zurücksetzen wenn über max. Count
        }
        // wenn in 4 Schritten nicht unter Threshold, dann ist es auch keine Deckelöffnung
        else if (openLid.fall_c >= 3 && (openLid.ref - this->temperature->getValue()) > OPL_THRESHOLD)
        { // Opened lid detected!            
            openLid.detected = true;
            openLid.temp = openLid.ref;
            // gas: shorter pause, a flame out looks like an open lid at first
            openLid.count = this->profile->flameOut ? OPL_PAUSE_FLAMEOUT : OPL_PAUSE; // TODO: check pause
            
            Log.notice("OPL: %d" CR, (int)(openLid.fall_c));
            Log.notice("OPL detected: %F" CR, (float)(this->temperature->getValue()));
            Log.notice("OPL Reference: %F" CR, (float)(openLid.temp));
        }
        
    }
    else
    {
        openLid.detected = false;
        openLid.fall_c = 0;
    }

    return openLid.detected;
}

boolean Pitmaster::getOPLStatus()
{
    return openLid.detected;
}

float Pitmaster::getOPLTemperature()
{
    return openLid.temp;
}

void Pitmaster::update()
{
    // finish a pending servo park (detach after the movement)
    this->serviceSafePark();

    this->checkOpenLid();

    if (false == this->checkDutyCycleTest())
        return;

    // Check Pitmaster Pause
    if (false == this->checkPause())
        return;

    if (false == this->checkAutoTune())
        return;

    // PITMASTER TYP
    switch (this->type)
    {
    case pm_off:
        this->disableActuators(true);
        break;
    case pm_auto:
        if (this->flameOutDetector.isTriggered())
        {
            // flame out: never drive further up, hold minOutput until acknowledged
            this->value = this->getMinOutput();
        }
        else if (false == this->openLid.detected)
        {
            this->value = max(this->pidCalc(), this->getMinOutput());
        }
        else
        {
            this->value = this->getMinOutput();
        }
        this->updateFlameOut();
        this->controlActuators();
        break;
    case pm_manual:
        this->controlActuators();
        break;
    }
}

void Pitmaster::updateFlameOut()
{
    if ((false == this->profile->flameOut) || this->flameOutDetector.isTriggered())
        return;

    if (false == this->temperature->isActive())
        return;

    this->flameOutDetector.configure(this->profile->foDrop, this->profile->foWindow);

    // the median filter is not fed while the lid is open, use the raw value then
    float x = this->openLid.detected ? this->temperature->getValue() : this->filteredTemp;

    if (this->flameOutDetector.update(x, this->targetTemperature, this->value, this->openLid.detected, millis()))
    {
        Log.error("Flame out detected! Holding minOutput" CR);
        this->value = this->getMinOutput();
        this->settingsChanged = true;
        this->events |= PM_EVENT_FLAMEOUT;
    }
}

void Pitmaster::controlActuators()
{
    float linkedvalue;

    // a new command supersedes a pending park
    this->parkPending = false;
    
    // JUMP DROSSEL (never while autotune runs)
    if (this->pid.isJump() && (false == this->isAutoTuneRunning()) && (this->value > this->profile->jumppw))
          this->value = this->profile->jumppw;

    initActuators();

    switch (this->profile->actuator)
    {
    case FAN:
        this->enableStepUp(true);
        this->controlFan(this->value, this->profile->dcmin, this->profile->dcmax);
        break;
    case SERVO:
        this->enableStepUp(false);
        this->controlServo(this->value, this->profile->spmin, this->profile->spmax);
        break;
    case SSR:
        this->enableStepUp(true);
        this->controlSSR(this->value, this->profile->dcmin, this->profile->dcmax);
        break;
    case DAMPER:
        this->enableStepUp(true);
        this->controlFan(this->value, this->profile->dcmin, this->profile->dcmax);
        switch(this->profile->link)
        {
            case 0: // degressiv link
                linkedvalue = (this->value > 0) ? 100 : 0;
                break;
            case 1: // linear link
                linkedvalue = (ceil(this->value * 0.1)) * 10.0;
                break;
            case 2: // lowpass link
                linkedvalue = constrain(((ceil(this->value * 5 * 0.03))*34.0), 0 ,100);
                break;
            default:
                break;
        }

        //Log.notice("OUTOUT_VALUE: %F" CR, (float)(this->value));
        //Log.notice("DAMPER_VALUE: %F" CR, (float)(linkedvalue));

        this->controlServo(linkedvalue, this->profile->spmin, this->profile->spmax);

        break;
    default:
        break;
    }
}

void Pitmaster::initActuators()
{
    switch (this->profile->actuator)
    {
    case FAN:
        if (initActuator != FAN)
        {
            ledcDetachPin(this->ioPin1);
            ledcDetachPin(this->ioPin2);
            dacWrite(this->ioPin1, 0u);
            initActuator = FAN;
        }
        break;
    case SERVO:
        if (initActuator != SERVO)
        {
            dacWrite(this->ioPin1, 0u);
            ledcDetachPin(this->ioPin1);
            ledcDetachPin(this->ioPin2);
            ledcSetup(this->channel2, SERVO_FREQUENCY, SERVO_BIT_RES);
            ledcAttachPin(this->ioPin2, this->channel2);
            ledcWrite(this->channel2, 0u);
            initActuator = SERVO;
        }
        break;
    case SSR:
        if (initActuator != SSR)
        {
            dacWrite(this->ioPin1, 0u);
            ledcDetachPin(this->ioPin1);
            ledcDetachPin(this->ioPin2);
            ledcSetup(this->channel1, SSR_FREQUENCY, SSR_BIT_RES);
            ledcAttachPin(this->ioPin1, this->channel1);
            ledcWrite(this->channel1, 0u);
            initActuator = SSR;
        }
        break;
    case DAMPER:
        if (initActuator != DAMPER)
        {
            ledcDetachPin(this->ioPin1);
            ledcDetachPin(this->ioPin2);
            dacWrite(this->ioPin1, 0u);
            ledcSetup(this->channel2, SERVO_FREQUENCY, SERVO_BIT_RES);
            ledcAttachPin(this->ioPin2, this->channel2);
            ledcWrite(this->channel2, 0u);
            initActuator = DAMPER;
        }
        break;
    default:
        break;
    }
}

void Pitmaster::controlFan(float newValue, float newDcMin, float newDcMax)
{
    static float prevValue = 0;
    // limits from global actor
    uint16_t dcmin = newDcMin * 10u; // 1. Nachkommastelle
    uint16_t dcmax = newDcMax * 10u; // 1. Nachkommastelle

    dcmin = map(dcmin, 0, 1000u, 0u, 0xFFu);
    dcmax = map(dcmax, 0, 1000u, 0u, 0xFFu);

    uint32_t newDc = map(newValue, 0, 100, dcmin, dcmax);

    // boost when coming from 0
    if ((0 == prevValue) && (newValue < FAN_BOOST_VALUE) && (newValue > 0))
    {
        newDc = map(FAN_BOOST_VALUE, 0, 100, dcmin, dcmax);
    }

    if (0u == newValue)
    {
        dacWrite(this->ioPin1, 0u);
    }
    else
    {
        dacWrite(this->ioPin1, newDc);
    }

    prevValue = newValue;
}

void Pitmaster::controlServo(float newValue, float newSPMin, float newSPMax)
{
    // never below spmin or above spmax
    newValue = constrain(newValue, (float)PITMIN, (float)PITMAX);

    // limits from global actor
    uint16_t spmin = newSPMin * 10u; // 1. Nachkommastelle
    uint16_t spmax = newSPMax * 10u; // 1. Nachkommastelle

    uint16_t servoPulseMin = (uint16_t)(this->servoDcMin / PM_SERVO_DUTY_CYCLE_DIVISOR);
    uint16_t servoPulseMax = (uint16_t)(this->servoDcMax / PM_SERVO_DUTY_CYCLE_DIVISOR);

    //Serial.printf("Pitmaster::controlServo: servoPulseMin = %d\n", servoPulseMin);
    //Serial.printf("Pitmaster::controlServo: servoPulseMax = %d\n", servoPulseMax);

    spmin = map(spmin, 0, 1000, servoPulseMin, servoPulseMax);
    spmax = map(spmax, 0, 1000, servoPulseMin, servoPulseMax);

    uint32_t newSp = map(newValue, 0, 100, spmin, spmax);
    uint32_t prevSp = ledcRead(this->channel2);

    if (newSp != prevSp)
    {
        ledcWrite(this->channel2, newSp);
    }
}

void Pitmaster::controlSSR(float newValue, float newDcMin, float newDcMax)
{
    static float prevValue = 0;
    // limits from global actor
    uint16_t dcmin = newDcMin * 10u; // 1. Nachkommastelle
    uint16_t dcmax = newDcMax * 10u; // 1. Nachkommastelle

    dcmin = map(dcmin, 0, 1000u, 0u, 0xFFFFu);
    dcmax = map(dcmax, 0, 1000u, 0u, 0xFFFFu);

    uint32_t newDc = map(newValue, 0, 100, dcmin, dcmax);
    uint32_t prevDc = ledcRead(this->channel1);

    prevValue = newValue;

    if (0u == newValue)
    {
        ledcWrite(this->channel1, 0u);
    }
    else if (newDc != prevDc)
    {
        ledcWrite(this->channel1, newDc);
    }
}

void Pitmaster::enableStepUp(boolean enable)
{
    if (this->ioSupply != PITMASTER_NO_SUPPLY_IO)
    {
        if (enable)
            ioSupplyRequested |= (1u << this->globalIndex);
        else
            ioSupplyRequested &= ~(1u << globalIndex);

        digitalWrite(this->ioSupply, ioSupplyRequested ? 1u : 0u);
    }
}

void Pitmaster::startServoPark()
{
    // drive the servo to minOutput and detach after SERVO_PARK_TIME_MS
    float minOutput = this->getMinOutput();
    this->controlServo(minOutput, this->profile->spmin, this->profile->spmax);
    this->value = minOutput;
    this->parkPending = true;
    this->parkUntil = millis() + SERVO_PARK_TIME_MS;
    this->pidReset();
}

void Pitmaster::serviceSafePark()
{
    if (this->parkPending && ((int32_t)(millis() - this->parkUntil) >= 0))
    {
        this->parkPending = false;
        this->detachActuators();
        Log.notice("Servo parked and detached" CR);
    }
}

void Pitmaster::detachActuators()
{
    dacWrite(this->ioPin1, 0u);
    ledcDetachPin(this->ioPin1);
    ledcDetachPin(this->ioPin2);
    digitalWrite(this->ioPin1, LOW);
    digitalWrite(this->ioPin2, LOW);

    this->enableStepUp(false);
    initActuator = NOAR;
}

void Pitmaster::disableActuators(boolean allowdelay)
{
    // gas safety: a servo is never just cut off where it is, it is driven
    // to minOutput first and detached when the movement is done
    if (initActuator == SERVO)
    {
        if (false == this->parkPending)
            this->startServoPark();
        return;
    }

    if (true == allowdelay && initActuator == DAMPER)
    {
        this->controlServo(0, this->profile->spmin, this->profile->spmax);
        initActuator = NOAR;
        Serial.println("ServoOFF");
        return;
    }

    this->parkPending = false;
    this->detachActuators();

    this->pidReset();
    memset((void *)&this->openLid, 0u, sizeof(this->openLid));
}

boolean Pitmaster::isDutyCycleTestRunning()
{
    return (NULL != this->dutyCycleTest) ? true : false;
}

boolean Pitmaster::isAutoTuneRunning()
{
    return ((NULL != this->autoTune) && this->autoTune->isRunning()) ? true : false;
}

void Pitmaster::setSupplyPin(uint8_t ioPin)
{
    ioSupply = ioPin;
}

float Pitmaster::filterTemperature()
{
    float raw = this->temperature->getValue();

    // prime the median filter after a pause, it may contain stale values
    if (false == this->medianPrimed)
    {
        for (uint8_t i = 0u; i < MEDIAN_SIZE; i++)
            this->medianValue->AddValue(raw);
        this->medianPrimed = true;
    }

    this->filteredTemp = this->medianValue->AddValue(raw);
    return this->filteredTemp;
}

float Pitmaster::pidCalcFiltered(float x, bool jumpEnabled)
{
    float jpthres = JP_THRESHOLD_CELSIUS;
    if (Fahrenheit == this->temperature->getUnit())
    {
        jpthres = JP_THRESHOLD_FAHRENHEIT;
    }

    return this->pid.calc(x, this->targetTemperature, this->profile->kp, this->profile->ki, this->profile->kd,
                          (SSR == this->profile->actuator), jpthres, this->pause, jumpEnabled);
}

float Pitmaster::pidCalc()
{
    float x = this->filterTemperature(); // IST
    return this->pidCalcFiltered(x, true);
}

void Pitmaster::pidReset()
{
    this->pid.reset();
}
