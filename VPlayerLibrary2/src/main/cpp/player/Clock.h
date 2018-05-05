#ifndef CLOCK_H_
#define CLOCK_H_

extern "C" {
#include <libavutil/time.h>
#include <libavutil/avutil.h>
};
#include <android/log.h>

class Clock {
public:
    static double now();

    Clock(intptr_t * serial = NULL);
    ~Clock();

    void setPts(double pts, intptr_t serial = 0);
    void setTimeAt(double pts, double time, intptr_t serial = 0);
    void setSpeed(double speed);
    void updatePts();
    void syncToClock(Clock* clock);
    double getTimeSinceLastUpdate();
    double getPts();

    double speed;
    bool paused;

    intptr_t serial() {
        return mSerial;
    }

private:
    intptr_t * mQueueSerial;
    double mBasePts;
    double mPtsDrift;
    double mLastUpdated;
    intptr_t mSerial;
};

#endif //CLOCK_H_
