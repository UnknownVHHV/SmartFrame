#pragma once
#include <Arduino.h>

#define SETS_TMR_INTERVAL 0
#define SETS_TMR_TIMEOUT 1
#define SETS_TMR_INTERVAL_OFF 2
#define SETS_TMR_TIMEOUT_OFF 3

namespace sets {
class Timer {
    typedef std::function<void()> TimerCallback;
    typedef unsigned long (*Uptime)();

public:
    Timer() {}
    Timer(uint32_t ms) { setTime(ms); startInterval(); }
    Timer(uint32_t ms, uint32_t seconds, uint32_t minutes = 0, uint32_t hours = 0, uint32_t days = 0) {
        setTime(ms, seconds, minutes, hours, days);
        startInterval();
    }

    void setTime(uint32_t time) { prd = time; }
    void setTime(uint32_t ms, uint32_t seconds, uint32_t minutes = 0, uint32_t hours = 0, uint32_t days = 0) {
        prd = seconds;
        if (minutes) prd += minutes * 60ul;
        if (hours) prd += hours * 3600ul;
        if (days) prd += days * 86400ul;
        if (prd) prd *= 1000ul;
        prd += ms;
    }

    uint32_t getTime() { return prd; }
    void setSource(Uptime source) { uptime = source; }
    void keepPhase(bool keep) { phaseF = keep; }
    void startTimeout() { mode = SETS_TMR_TIMEOUT; restart(); }
    void startTimeout(uint32_t time) { prd = time; startTimeout(); }
    void startInterval() { mode = SETS_TMR_INTERVAL; restart(); }
    void startInterval(uint32_t time) { prd = time; startInterval(); }
    void restart() { if (prd) { tmr = uptime(); if (mode > 1) mode -= 2; } }
    void stop() { if (mode < 2) mode += 2; }
    bool state() { return (mode < 2); }
    void attach(TimerCallback callback) { this->callback = callback; }
    void detach() { callback = nullptr; }
    uint32_t timeLeft() { return state() ? (uptime() - tmr) : 0; }
    bool tick() {
        if (state() && uptime() - tmr >= prd) {
            if (callback) callback();
            if (mode == SETS_TMR_INTERVAL) _reset();
            else stop();
            return true;
        }
        return false;
    }
    operator bool() { return tick(); }

private:
    uint8_t mode = SETS_TMR_INTERVAL_OFF;
    uint32_t tmr = 0, prd = 0;
    TimerCallback callback = nullptr;
    Uptime uptime = millis;
    bool phaseF = false;

    void _reset() { if (phaseF) tmr += prd; else tmr = uptime(); }
};
}