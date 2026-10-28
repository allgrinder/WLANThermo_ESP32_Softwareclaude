// Stub of ArduinoLog for the host simulator (GPL-3.0, WLANThermo gas fork)
#pragma once
#include <stdio.h>

#define CR "\n"

extern bool gSimLogEnabled;

class SimLog
{
public:
  template <typename... Args>
  void notice(const char *fmt, Args...) { if (gSimLogEnabled) printf("[N] %s", fmt); }
  template <typename... Args>
  void error(const char *fmt, Args...) { if (gSimLogEnabled) printf("[E] %s", fmt); }
  template <typename... Args>
  void warning(const char *fmt, Args...) {}
  template <typename... Args>
  void verbose(const char *fmt, Args...) {}
  template <typename... Args>
  void trace(const char *fmt, Args...) {}
};
extern SimLog Log;
