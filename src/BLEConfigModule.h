#pragma once

#include <Arduino.h>

void bleConfigBegin(const String& deviceName);
void bleConfigStop();
void bleConfigNotify(const String& message);
