#pragma once

#include "configuration.h"
#include <Arduino.h>

struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    
    // Constructor for easy initialization
    DateTime(uint16_t y = 2025, uint8_t mo = 1, uint8_t d = 1, 
             uint8_t h = 0, uint8_t mi = 0, uint8_t s = 0) 
        : year(y), month(mo), day(d), hour(h), minute(mi), second(s) {}
};

class SimpleTimeManager {
private:
    uint32_t timeSetAtMillis;      // When setTime was called (millis())
    uint32_t timeSetToSeconds;     // What time it was set to (seconds since epoch)
    bool timeIsSet;                // Whether time has been set
    
    // Helper functions
    bool isLeapYear(uint16_t year);
    uint8_t getDaysInMonth(uint8_t month, uint16_t year);
    uint32_t dateTimeToSeconds(const DateTime& dt);
    DateTime secondsToDateTime(uint32_t seconds);
    
public:
    SimpleTimeManager();
    
    // Main functions
    bool setTime(const char* timeString);  // "mm/dd/yyyy hh:mm:ss"
    bool setTime(uint16_t year, uint8_t month, uint8_t day, 
                 uint8_t hour, uint8_t minute, uint8_t second);
    bool setTime(const DateTime& dt);
    
    DateTime getTime();
    String getTimeString();  // Returns "mm/dd/yyyy hh:mm:ss"
    String getTimeStringShort();  // Returns "mm/dd hh:mm"
    
    uint32_t getCurrentSeconds();  // Seconds since epoch
    bool isTimeSet() { return timeIsSet; }
    
    // Utility functions
    String formatTime(const DateTime& dt);
    uint32_t getUptime() { return millis(); }  // Milliseconds since boot
    
    // For logging - get time as timestamp
    uint32_t getTimestamp() { return getCurrentSeconds(); }
};

SimpleTimeManager* getTimeManager();