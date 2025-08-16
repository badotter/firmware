#include "SimpleTimeManager.h"

// Days in each month (non-leap year)
const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Static instance for singleton-like behavior
static SimpleTimeManager* timeManager = nullptr;

// Global accessor function
SimpleTimeManager* getTimeManager() {
    if (!timeManager) {
        timeManager = new SimpleTimeManager();
        LOG_INFO("Time manager initialized");
    }
    return timeManager;
}

SimpleTimeManager::SimpleTimeManager() {
    timeSetAtMillis = 0;
    timeSetToSeconds = 0;
    timeIsSet = false;
}

bool SimpleTimeManager::setTime(const char* timeString) {
    // Parse "mm/dd/yyyy hh:mm:ss"
    int month, day, year, hour, minute, second;
    
    LOG_DEBUG("Setting time from string: %s", timeString);
    int parsed = sscanf(timeString, "%d/%d/%d %d:%d:%d", 
                       &month, &day, &year, &hour, &minute, &second);
    LOG_ERROR("What we got. %d/%d/%d %d:%d:%d", month, day, year, hour, minute, second);
    if (parsed != 6) {
        LOG_ERROR("Invalid time format. Use: mm/dd/yyyy hh:mm:ss");
        
        return false;
    }
    
    // Validate ranges
    if (month < 1 || month > 12 || 
        day < 1 || day > 31 || 
        year < 2024 || year > 2050 ||
        hour > 23 || minute > 59 || second > 59) {
        LOG_ERROR("Invalid time values");
        return false;
    }

    timeIsSet = true;
    return setTime(year, month, day, hour, minute, second);
}

bool SimpleTimeManager::setTime(uint16_t year, uint8_t month, uint8_t day, 
                                uint8_t hour, uint8_t minute, uint8_t second) {
    DateTime dt(year, month, day, hour, minute, second);
    return setTime(dt);
}

bool SimpleTimeManager::setTime(const DateTime& dt) {
    // Validate the date/time
    if (dt.month < 1 || dt.month > 12 || 
        dt.day < 1 || dt.day > getDaysInMonth(dt.month, dt.year) ||
        dt.hour > 23 || dt.minute > 59 || dt.second > 59) {
        LOG_ERROR("Invalid date/time values");
        return false;
    }
    
    timeSetAtMillis = millis();
    timeSetToSeconds = dateTimeToSeconds(dt);
    timeIsSet = true;
    
    LOG_INFO("Time set to: %s", formatTime(dt).c_str());
    return true;
}

DateTime SimpleTimeManager::getTime() {
    if (!timeIsSet) {
        LOG_WARN("Time not set, returning default");
        return DateTime(); // Returns 2024/01/01 00:00:00
    }
    
    // Calculate current time
    uint32_t elapsedMillis = millis() - timeSetAtMillis;
    uint32_t elapsedSeconds = elapsedMillis / 1000;
    uint32_t currentSeconds = timeSetToSeconds + elapsedSeconds;
    
    return secondsToDateTime(currentSeconds);
}

String SimpleTimeManager::getTimeString() {
    return formatTime(getTime());
}

String SimpleTimeManager::getTimeStringShort() {
    DateTime dt = getTime();
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d/%02d %02d:%02d", 
             dt.month, dt.day, dt.hour, dt.minute);
    return String(buffer);
}

uint32_t SimpleTimeManager::getCurrentSeconds() {
    if (!timeIsSet) {
        return 0;
    }
    
    uint32_t elapsedMillis = millis() - timeSetAtMillis;
    uint32_t elapsedSeconds = elapsedMillis / 1000;
    return timeSetToSeconds + elapsedSeconds;
}

String SimpleTimeManager::formatTime(const DateTime& dt) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d %02d:%02d:%02d", 
             dt.month, dt.day, dt.year, dt.hour, dt.minute, dt.second);
    return String(buffer);
}

bool SimpleTimeManager::isLeapYear(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t SimpleTimeManager::getDaysInMonth(uint8_t month, uint16_t year) {
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return daysInMonth[month - 1];
}

uint32_t SimpleTimeManager::dateTimeToSeconds(const DateTime& dt) {
    // Calculate seconds since Unix epoch (Jan 1, 1970)
    // This is a simplified calculation - good enough for logging purposes
    
    uint32_t days = 0;
    
    // Add days for complete years since 1970
    for (uint16_t year = 1970; year < dt.year; year++) {
        days += isLeapYear(year) ? 366 : 365;
    }
    
    // Add days for complete months in current year
    for (uint8_t month = 1; month < dt.month; month++) {
        days += getDaysInMonth(month, dt.year);
    }
    
    // Add remaining days (minus 1 because day 1 = 0 days elapsed)
    days += dt.day - 1;
    
    // Convert to seconds and add time of day
    uint32_t seconds = days * 86400UL;  // 24 * 60 * 60
    seconds += dt.hour * 3600UL;        // 60 * 60
    seconds += dt.minute * 60UL;
    seconds += dt.second;
    
    return seconds;
}

DateTime SimpleTimeManager::secondsToDateTime(uint32_t seconds) {
    // Convert seconds since Unix epoch back to DateTime
    // This is the reverse of dateTimeToSeconds()
    
    DateTime dt;
    
    // Extract time of day first
    uint32_t secondsInDay = seconds % 86400UL;
    dt.hour = secondsInDay / 3600;
    dt.minute = (secondsInDay % 3600) / 60;
    dt.second = secondsInDay % 60;
    
    // Calculate date
    uint32_t days = seconds / 86400UL;
    
    // Find the year
    dt.year = 1970;
    while (true) {
        uint32_t daysInYear = isLeapYear(dt.year) ? 366 : 365;
        if (days < daysInYear) {
            break;
        }
        days -= daysInYear;
        dt.year++;
    }
    
    // Find the month
    dt.month = 1;
    while (dt.month <= 12) {
        uint32_t daysInThisMonth = getDaysInMonth(dt.month, dt.year);
        if (days < daysInThisMonth) {
            break;
        }
        days -= daysInThisMonth;
        dt.month++;
    }
    
    // Remaining days is the day of month (add 1 because day 1 = 0 days elapsed)
    dt.day = days + 1;
    
    return dt;
}