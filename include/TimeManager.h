#pragma once

#include <Arduino.h>
#include <time.h>

// Интерфейс модуля времени
void initTime();
String getCurrentTimeStr();     // Возвращает "HH:MM:SS" для верхнего статус-бара
String getCurrentTimeShort();    // Возвращает "HH:MM" для сравнения с расписанием
uint32_t getCurrentDayOfYear(); // Returns day of year (1-366) for daily schedule tracking
bool isTimeValid();             // Возвращает true, если время успешно синхронизировано с интернетом