#include "TimeManager.h"

// Строка конфигурации часового пояса (POSIX формат)
// "MSK-3" означает Moscow Time, смещение на 3 часа вперед относительно UTC, без перехода на летнее время
const char* TZ_INFO = "MSK-3"; 
const char* NTP_SERVER = "pool.ntp.org";

void initTime() {
    Serial.println("[Time] Настройка встроенного NTP клиента...");
    
    // Настраиваем системный часовой пояс
    configTime(TZ_INFO, NTP_SERVER);
    
    // При старте в режиме STA контроллер начнет стучаться на NTP сервер автоматически в фоновом режиме
}

bool isTimeValid() {
    time_t now = time(nullptr);
    // Если системное время больше, чем 1 января 2020 года, значит синхронизация успешно прошла
    return now > 1577836800; 
}

String getCurrentTimeStr() {
    if (!isTimeValid()) return "--:--:--";

    time_t now = time(nullptr);
    struct tm* timeInfo = localtime(&now);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
    return String(buf);
}

String getCurrentTimeShort() {
    if (!isTimeValid()) return "00:00";

    time_t now = time(nullptr);
    struct tm* timeInfo = localtime(&now);

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
    return String(buf);
}

uint32_t getCurrentDayOfYear() {
    if (!isTimeValid()) return 0;

    time_t now = time(nullptr);
    struct tm* timeInfo = localtime(&now);

    // tm_yday возвращает день от начала года (0-365). Прибавляем 1, чтобы получить 1-366.
    return (uint32_t)(timeInfo->tm_yday + 1);
}
