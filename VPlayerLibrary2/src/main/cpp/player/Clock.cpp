#include "Clock.h"

/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

#define sTag "NativeClock"

Clock::Clock(intptr_t *serial)  :
        mSpeed(1),
        mSerial(0),
        mQueueSerial(serial),
        mBasePts(0),
        mPtsDrift(0),
        mLastUpdated(0),
        paused(false) {
    setTimeAt(-1, now(), 0);
    if (serial == NULL) {
        mQueueSerial = &mSerial;
    }
}

Clock::~Clock() {
}

double Clock::now() {
    return av_gettime_relative() / (double) AV_TIME_BASE;
}

void Clock::setPts(double pts, intptr_t serial) {
    setTimeAt(pts, now(), serial);
}

void Clock::setTimeAt(double pts, double time, intptr_t serial) {
    mBasePts = pts;
    mPtsDrift = mBasePts != -1 ? pts - time : -1;
    mLastUpdated = time;
    if (serial) {
        mSerial = serial;
    }
}

void Clock::setSpeed(double speed) {
    updatePts();
    mSpeed = speed;
}

void Clock::syncToClock(Clock* clock) {
    double pts = getPts();
    double theirPts = clock->getPts();
    if (!isnan(theirPts) && (isnan(pts) || fabs(pts - theirPts) > AV_NOSYNC_THRESHOLD)) {
        setPts(theirPts, clock->mSerial);
    }
}

void Clock::updatePts() {
    setPts(getPts());
}

double Clock::getTimeSinceLastUpdate() {
    return now() - mLastUpdated;
}

double Clock::getPts() {
    if (*mQueueSerial != mSerial) {
        return NAN;
    } else if (paused) {
        return mBasePts;
    } else {
        double time = now();
        return mPtsDrift + time - (time - mLastUpdated) * (1.0 - mSpeed);
    }
}










