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

#include "PitmasterGrp.h"
#include "system/SystemBase.h"
#include "Settings.h"
#include "ArduinoLog.h"

PitmasterGrp::PitmasterGrp()
{
  this->addIndex = 0;
  this->enabled = true;
  this->shutdownLock = false;

  for (uint8_t i = 0u; i < MAX_PITMASTERS; i++)
    pitmasters[i] = NULL;
}

void PitmasterGrp::add(Pitmaster *pitmaster)
{
  pitmasters[addIndex++] = pitmaster;
}

void PitmasterGrp::update()
{
  if (this->enabled != true)
  {
    // pitmasters are disabled, but a started servo park has to be finished
    for (uint8_t i = 0; i < MAX_PITMASTERS; i++)
    {
      if (pitmasters[i] != NULL)
        pitmasters[i]->serviceSafePark();
    }
    return;
  }

  Log.verbose("PitmasterGrp::update()" CR);

  boolean save = false;

  for (uint8_t i = 0; i < MAX_PITMASTERS; i++)
  {
    if (pitmasters[i] != NULL)
    {
      pitmasters[i]->update();
      pitmasters[i]->handleCallbacks();

      uint8_t events = pitmasters[i]->takeEvents();

      if (events & PM_EVENT_SAVE)
        save = true;

      if (events & PM_EVENT_FLAMEOUT)
      {
        TemperatureBase *temperature = pitmasters[i]->getAssignedTemperature();
        gSystem->notification.notifyFlameOut(TemperatureGrp::getIndex(temperature), pitmasters[i]->getTargetTemperature());
      }
    }
  }

  if (save)
    this->saveConfig();
}

void PitmasterGrp::parkAll()
{
  // used right before a restart: stop controlling and keep it stopped
  this->shutdownLock = true;
  this->enabled = false;

  for (uint8_t i = 0; i < MAX_PITMASTERS; i++)
  {
    if (pitmasters[i] != NULL)
    {
      pitmasters[i]->stopAutoTune(AtError::Disabled);
      pitmasters[i]->disableActuators(false);
    }
  }
}

boolean PitmasterGrp::isFlameOutAlarm()
{
  for (uint8_t i = 0; i < MAX_PITMASTERS; i++)
  {
    if ((pitmasters[i] != NULL) && pitmasters[i]->isFlameOut())
      return true;
  }

  return false;
}
uint8_t PitmasterGrp::count()
{
  return addIndex;
}

void PitmasterGrp::loadConfig()
{
  DynamicJsonBuffer jsonBuffer(Settings::jsonBufferSize);
  JsonObject &json = Settings::read(kPitmasters, &jsonBuffer);

  if (json.success())
  {
    JsonArray &_master = json["pm"];

    byte pitsize = 0;
    boolean restartDuringAutotune[MAX_PITMASTERS] = {false};

    for (JsonArray::iterator it = _master.begin(); it != _master.end(); ++it)
    {
      Pitmaster *pm = pitmasters[pitsize];
      if (pm != NULL)
      {
        pm->assignTemperature(gSystem->temperatures[_master[pitsize]["ch"]]);
        pm->assignProfile(gSystem->getPitmasterProfile(_master[pitsize]["pid"].as<uint8_t>()));
        pm->setTargetTemperature(_master[pitsize]["set"]);
        pm->setType((PitmasterType)_master[pitsize]["act"].as<uint8_t>());
        
        if(_master[pitsize].asObject().containsKey("act_last"))
          pm->setTypeLast((PitmasterType)_master[pitsize]["act_last"].as<uint8_t>());

        if (pm->getType() == pm_manual)
          pm->setValue(_master[pitsize]["val"].as<float>());

        if(_master[pitsize].asObject().containsKey("dCount"))
          pm->setDCount(_master[pitsize]["dCount"].as<uint8_t>());

        if(_master[pitsize].asObject().containsKey("servoDcMin"))
          pm->setServoMinDutyCyle(_master[pitsize]["servoDcMin"].as<uint16_t>());        
        if(_master[pitsize].asObject().containsKey("servoDcMax"))
          pm->setServoMaxDutyCyle(_master[pitsize]["servoDcMax"].as<uint16_t>());

        // learned output table of the smart control
        if (_master[pitsize].asObject().containsKey("bm"))
        {
          BiasMap *map = pm->getBiasMap();
          map->clear();
          map->setProfileId(_master[pitsize]["bmp"].as<uint8_t>());
          JsonArray &_bm = _master[pitsize]["bm"];
          for (uint8_t b = 0u; (b < _bm.size()) && (b < BIASMAP_BINS); b++)
          {
            int16_t v = _bm[b].as<int16_t>();
            if (v >= 0)
              map->setBin(b, v / 10.0f, BIASMAP_KNOWN_S);
          }
        }

        // restart during a running autotune: hold minOutput, see Pitmaster
        restartDuringAutotune[pitsize] = (_master[pitsize].asObject().containsKey("at") && _master[pitsize]["at"].as<uint8_t>());
      }

      pitsize++;
    }

    JsonArray &_pid = json["pid"];

    uint8_t pidsize = 0;

    // Wie viele Pitmaster sind vorhanden
    for (JsonArray::iterator it = _pid.begin(); it != _pid.end(); ++it)
    {
      PitmasterProfile *profile = gSystem->getPitmasterProfile(pidsize);
      if (profile != NULL)
      {
        profile->name = _pid[pidsize]["name"].asString();
        profile->id = _pid[pidsize]["id"];
        profile->actuator = _pid[pidsize]["aktor"];
        profile->kp = _pid[pidsize]["Kp"];
        profile->ki = _pid[pidsize]["Ki"];
        profile->kd = _pid[pidsize]["Kd"];
        profile->dcmin = _pid[pidsize]["DCmin"];
        profile->dcmax = _pid[pidsize]["DCmax"];
        profile->jumppw = _pid[pidsize]["jp"];
        profile->spmin = _pid[pidsize]["SPmin"];
        profile->spmax = _pid[pidsize]["SPmax"];
        profile->link = _pid[pidsize]["link"];
        profile->opl = _pid[pidsize]["ol"];

        // gas fork extensions, keep defaults when not stored
        JsonObject &_p = _pid[pidsize].asObject();
        if (_p.containsKey("mo"))
          profile->minOutput = _p["mo"];
        if (_p.containsKey("atd"))
          profile->atD = _p["atd"];
        if (_p.containsKey("ath"))
          profile->atH = _p["ath"];
        if (_p.containsKey("atr"))
          profile->atRule = _p["atr"];
        if (_p.containsKey("fo"))
          profile->flameOut = _p["fo"];
        if (_p.containsKey("fod"))
          profile->foDrop = _p["fod"];
        if (_p.containsKey("fow"))
          profile->foWindow = _p["fow"];
        if (_p.containsKey("sm"))
          profile->smart = _p["sm"];
      }

      pidsize++;
    }

    // profiles are loaded now, handle an autotune interrupted by a restart
    for (uint8_t i = 0u; i < MAX_PITMASTERS; i++)
    {
      if ((pitmasters[i] != NULL) && restartDuringAutotune[i])
        pitmasters[i]->markAutoTuneRestartAbort();
    }
  }
}

void PitmasterGrp::saveConfig()
{
  DynamicJsonBuffer jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();
  JsonArray &_master = json.createNestedArray("pm");

  for (int i = 0; i < MAX_PITMASTERS; i++)
  {
    Pitmaster *pm = gSystem->pitmasters[i];
    if (pm != NULL)
    {
      JsonObject &_ma = _master.createNestedObject();
      _ma["ch"] = TemperatureGrp::getIndex(pm->getAssignedTemperature());
      _ma["pid"] = pm->getAssignedProfile()->id;
      _ma["set"] = double_with_n_digits(pm->getTargetTemperature(), 1);
      _ma["act"] = (uint8_t)pm->getType();
      _ma["act_last"] = (uint8_t)pm->getTypeLast();
      _ma["val"] = pm->getValue();
      _ma["dCount"] = pm->getDCount();
      _ma["servoDcMin"] = pm->getServoMinDutyCyle();
      _ma["servoDcMax"] = pm->getServoMaxDutyCyle();
      if (pm->isAutoTuneRunning())
        _ma["at"] = 1;

      BiasMap *map = pm->getBiasMap();
      if (map->knownCount() > 0u)
      {
        _ma["bmp"] = map->getProfileId();
        JsonArray &_bm = _ma.createNestedArray("bm");
        for (uint8_t b = 0u; b < BIASMAP_BINS; b++)
          _bm.add(map->isKnown(b) ? (int16_t)(map->getValue(b) * 10.0f + 0.5f) : (int16_t)-1);
      }
    }
  }

  JsonArray &_pit = json.createNestedArray("pid");

  for (int i = 0; i < gSystem->getPitmasterProfileCount(); i++)
  {
    PitmasterProfile *profile = gSystem->getPitmasterProfile(i);
    if (profile != NULL)
    {
      JsonObject &_pid = _pit.createNestedObject();
      _pid["name"] = profile->name;
      _pid["id"] = profile->id;
      _pid["aktor"] = profile->actuator;
      _pid["Kp"] = double_with_n_digits(profile->kp, 2);
      _pid["Ki"] = double_with_n_digits(profile->ki, 5);
      _pid["Kd"] = double_with_n_digits(profile->kd, 2);
      _pid["DCmin"] = double_with_n_digits(profile->dcmin, 1);
      _pid["DCmax"] = double_with_n_digits(profile->dcmax, 1);
      _pid["jp"] = profile->jumppw;
      _pid["SPmin"] = double_with_n_digits(profile->spmin, 1);
      _pid["SPmax"] = double_with_n_digits(profile->spmax, 1);
      _pid["link"] = profile->link;
      _pid["ol"] = profile->opl;
      _pid["mo"] = double_with_n_digits(profile->minOutput, 1);
      _pid["atd"] = double_with_n_digits(profile->atD, 1);
      _pid["ath"] = double_with_n_digits(profile->atH, 1);
      _pid["atr"] = profile->atRule;
      _pid["fo"] = profile->flameOut;
      _pid["fod"] = double_with_n_digits(profile->foDrop, 1);
      _pid["fow"] = profile->foWindow;
      _pid["sm"] = profile->smart;
    }
  }

  Settings::write(kPitmasters, json);
}

void PitmasterGrp::enable(boolean enabled)
{
  if ((this->enabled == enabled) || this->shutdownLock)
    return;

  if (enabled != true)
  {
    for (uint8_t i = 0; i < MAX_PITMASTERS; i++)
    {
      if (pitmasters[i] != NULL)
      {
        pitmasters[i]->stopAutoTune(AtError::Disabled);
        pitmasters[i]->disableActuators(false);
      }
    }
  }

  this->enabled = enabled;
}

Pitmaster *PitmasterGrp::getActivePitmaster(TemperatureBase *temperature)
{
  Pitmaster *pit = NULL;

  for (uint8_t i = 0u; i < MAX_PITMASTERS; i++)
  {
    if (pitmasters[i] != NULL)
    {
      if ((pitmasters[i]->getAssignedTemperature()) == temperature && (PitmasterType::pm_auto == pitmasters[i]->getType()))
      {
        pit = pitmasters[i];
        break;
      }
    }
  }

  return pit;
}

Pitmaster *PitmasterGrp::operator[](int index)
{
  return (index < MAX_PITMASTERS) ? pitmasters[index] : NULL;
}