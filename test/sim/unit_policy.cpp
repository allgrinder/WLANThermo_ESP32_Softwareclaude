// Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
// Unit test of src/ServerApiPolicy.h with ArduinoJson 5 (as in the firmware)
#include <iostream>
#include <string>
#include "ArduinoJson.h"
#include "../../src/ServerApiPolicy.h"

static int failures = 0;
static void check(bool ok, const std::string &what)
{
  std::cout << (ok ? "  ok:   " : "  FAIL: ") << what << std::endl;
  failures += ok ? 0 : 1;
}

static std::string apply(const char *answer, bool updateRequest, bool customServer)
{
  DynamicJsonBuffer buffer;
  JsonObject &json = buffer.parseObject(answer);
  uint8_t policy = serverAnswerPolicy(updateRequest, customServer);
  if (!(policy & SERVER_ANSWER_PARSE))
    return "<not parsed>";
  filterServerAnswer(json, policy);
  std::string out;
  json.printTo(out);
  return out;
}

int main()
{
  std::cout << "Server answer policy (S3)" << std::endl;
  const char *evil = "{\"url\":{\"api\":{\"host\":\"evil\"}},\"update\":{\"available\":true,\"force\":true},\"cloud\":{\"task\":true}}";
  check(apply(evil, false, false) == "{\"cloud\":{\"task\":true}}", "cloud answer: url and update removed");
  check(apply(evil, true, false) == "{\"update\":{\"available\":true,\"force\":true},\"cloud\":{\"task\":true}}",
        "update answer: update kept, url removed");
  check(apply(evil, true, true) == "<not parsed>", "custom cloud answer: not parsed");
  check(apply(evil, false, true) == "<not parsed>", "custom cloud answer (data): not parsed");
  {
    DynamicJsonBuffer buffer;
    JsonObject &json = buffer.parseObject(evil);
    filterServerAnswer(json, SERVER_ANSWER_ALL);
    check(json.containsKey("url") && json.containsKey("update"), "local /setapi: everything allowed");
  }
  std::cout << (failures ? "FAILED" : "ALL PASSED") << std::endl;
  return failures ? 1 : 0;
}
