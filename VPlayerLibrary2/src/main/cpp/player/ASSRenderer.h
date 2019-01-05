#ifndef VPLAYER_LIB2_ASSRENDERER_H
#define VPLAYER_LIB2_ASSRENDERER_H

#include "ASSBitmap.h"
#include <vector>

class ASSRenderer {
public:
    ASSRenderer();
    ~ASSRenderer();

    void setSize(int width, int height);

    void setDefaultFont(const char* fontPath, const char* fontFamilyName);

    void addFont(char* name, char* data, int size);

    ASS_Track* createTrack(const char* data, int size);

    ASS_Image* renderFrame(ASS_Track *track, long long time, int *changed);

    ASSBitmap** getBitmaps(ASS_Track *track, long long time, int* size, int* changed);

    int getError();

private:
    void swapBuffers();

    void ensureTmpBufferCapacity(int size);

    int mError;
    ASS_Library* mAssLibrary;
    ASS_Renderer* mAssRenderer;

    // Buffers, returned swap so that differences can be detected
    ASSBitmap** mBitmapBuffer;
    int mBitmapCapacity;
    int mBitmapCount;

    ASSBitmap** mTmpBitmapBuffer;
    int mTmpBitmapCapacity;
    int mTmpBitmapCount;

    char* mDefaultFontPath;
    size_t mDefaultFontPathCapacity;
    char* mDefaultFontName;
    size_t mDefaultFontNameCapacity;
};

#endif // VPLAYER_LIB2_ASSRENDERER_H