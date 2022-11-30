#include "clock.h"

Clock::Clock() {
    restart();
}

double Clock::getSeconds() const {
    return getMilliseconds() / 1000.0;
}

double Clock::getMilliseconds() const {
    return double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count()) / 1000.0;
}

void Clock::setSeconds(double time) {
    restart();
    setMilliseconds(time * 1000.0);
}

void Clock::setMilliseconds(double time) {
    restart();
    start -= std::chrono::microseconds(int64_t(time * 1000.0));
}

void Clock::sleepSeconds(int64_t time) {
    std::this_thread::sleep_for(std::chrono::seconds(time));
}

void Clock::sleepMilliseconds(int64_t time) {
    std::this_thread::sleep_for(std::chrono::milliseconds(time));
}