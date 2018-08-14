#ifndef IAUDIORENDERER_H
#define IAUDIORENDERER_H

#include <libavutil/frame.h>

class IAudioRenderer {
public:
    virtual int write(uint8_t *data, int len) = 0;
    virtual int pause() = 0;
    virtual int play() = 0;
    virtual int flush() = 0;
    virtual int stop() = 0;

    virtual int numChannels() = 0;
    virtual int sampleRate() = 0;
    virtual int64_t layout() = 0;
    virtual enum AVSampleFormat format() = 0;

    /**
     * Get the audio latency when writing to be displayed in seconds
     * @return latency in seconds
     */
    virtual double getLatency() = 0;
    virtual double updateLatency(bool force = false) = 0;
};

#endif //IAUDIORENDERER_H
