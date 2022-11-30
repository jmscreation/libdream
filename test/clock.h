#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <chrono>
#include <thread>

typedef std::chrono::time_point<std::chrono::system_clock> timepoint;

class Clock {
    timepoint start;
public:
    Clock();
    virtual ~Clock()=default;
    double getSeconds() const;
    double getMilliseconds() const;
    inline void restart() { start = std::chrono::system_clock::now(); }
    void setSeconds(double time);
    void setMilliseconds(double time);
    
    static void sleepSeconds(int64_t time);
    static void sleepMilliseconds(int64_t time);
};

#endif // __CLOCK_H__