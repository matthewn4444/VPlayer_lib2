#include "ASSRenderer.h"
#include <android/log.h>
#include <libavutil/error.h>

#define DEFAULT_BUFFER_SIZE 3

static const char *sTag = "ASSRenderer";

static void ass_log(int ass_level, const char *fmt, va_list args, void *ctx) {
//    __android_log_print(ANDROID_LOG_VERBOSE, sTag, fmt, args);
}

ASSRenderer::ASSRenderer() :
        mError(0),
        mAssLibrary(nullptr),
        mAssRenderer(nullptr),
        mBitmapBuffer(nullptr),
        mBitmapCount(0),
        mBitmapCapacity(0),
        mTmpBitmapBuffer(nullptr),
        mTmpBitmapCount(0),
        mTmpBitmapCapacity(0),
        mDefaultFontPath(nullptr),
        mDefaultFontPathCapacity(0),
        mDefaultFontName(nullptr),
        mDefaultFontNameCapacity(0) {
    mAssLibrary = ass_library_init();
    if (!mAssLibrary) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass library");
        mError = AVERROR(ENOMEM);
        return;
    }
    ass_set_message_cb(mAssLibrary, ass_log, nullptr);
    ass_set_extract_fonts(mAssLibrary, true);

    mAssRenderer = ass_renderer_init(mAssLibrary);
    if (!mAssRenderer) {
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass renderer");
        mError = AVERROR(ENOMEM);
        return;
    }
    ass_set_fonts(mAssRenderer, nullptr, nullptr, ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
}

ASSRenderer::~ASSRenderer() {
    if (mAssRenderer) {
        ass_renderer_done(mAssRenderer);
        mAssRenderer = nullptr;
    }
    if (mAssLibrary) {
        ass_library_done(mAssLibrary);
        mAssLibrary = nullptr;
    }
    if (mBitmapBuffer) {
        for (int i = 0; i < mBitmapCapacity; ++i) {
            delete mBitmapBuffer[i];
        }
        delete[] mBitmapBuffer;
        mBitmapBuffer = nullptr;
    }
    if (mTmpBitmapBuffer) {
        for (int i = 0; i < mTmpBitmapCapacity; ++i) {
            delete mTmpBitmapBuffer[i];
        }
        delete[] mTmpBitmapBuffer;
        mTmpBitmapBuffer = nullptr;
    }
    if (mDefaultFontPath) {
        delete[] mDefaultFontPath;
        mDefaultFontPath = nullptr;
    }
    if (mDefaultFontName) {
        delete[] mDefaultFontName;
        mDefaultFontName = nullptr;
    }
}

void ASSRenderer::setSize(int width, int height) {
    if (mAssRenderer) {
        ass_set_frame_size(mAssRenderer, width, height);
    }
}

void ASSRenderer::setDefaultFont(const char *fontPath, const char *fontFamilyName) {
    if (mAssRenderer) {
        size_t size = strlen(fontPath) + 1;
        if (mDefaultFontPath == nullptr || size > mDefaultFontPathCapacity) {
            delete[] mDefaultFontPath;
            mDefaultFontPath = new char[size];
            strncpy(mDefaultFontPath, fontPath, size);
            mDefaultFontPathCapacity = size;
        }
        size = strlen(fontFamilyName) + 1;
        if (mDefaultFontName == nullptr || size > mDefaultFontNameCapacity) {
            delete[] mDefaultFontName;
            mDefaultFontName = new char[size];
            strncpy(mDefaultFontName, fontFamilyName, size);
            mDefaultFontNameCapacity = size;
        }
        ass_set_fonts(mAssRenderer, mDefaultFontPath, mDefaultFontName, ASS_FONTPROVIDER_AUTODETECT,
                      nullptr, 1);
    }
}

void ASSRenderer::addFont(char *name, char *data, int size) {
    if (mAssLibrary) {
        ass_add_font(mAssLibrary, name, data, size);
        ass_set_fonts(mAssRenderer, mDefaultFontPath, mDefaultFontName, ASS_FONTPROVIDER_AUTODETECT,
                      nullptr, 1);
    }
}

ASS_Track *ASSRenderer::createTrack(const char *data, int size) {
    if (mAssLibrary) {
        ASS_Track *track = ass_new_track(mAssLibrary);
        if (track) {
            if (data) {
                ass_process_codec_private(track, const_cast<char *>(data), size);
            }
            return track;
        }
        __android_log_print(ANDROID_LOG_ERROR, sTag, "Cannot allocate ass track");
    }
    return nullptr;
}

ASS_Image *ASSRenderer::renderFrame(ASS_Track *track, long long time, int *changed) {
    return ass_render_frame(mAssRenderer, track, time, changed);
}

int ASSRenderer::getError() {
    return mError;
}

ASSBitmap **ASSRenderer::getBitmaps(ASS_Track *track, long long time, int *size,
        int *changed) {
    if (size) {
        *size = 0;
    }
    mTmpBitmapCount = 0;

    ASS_Image *images = ass_render_frame(mAssRenderer, track, time, changed);
    if (*changed == 0 || images == nullptr) {
        if (track->n_events == 0) {
            *changed = 0;
        } else if (*changed == 2) {
            // When clear screen, swap buffers to hold last data
            swapBuffers();
        }
        // No change or image not available, leave now
        return nullptr;
    }

    // Put the first object in
    if (mTmpBitmapBuffer == nullptr) {
        mTmpBitmapBuffer = new ASSBitmap *[DEFAULT_BUFFER_SIZE];
        mTmpBitmapCapacity = DEFAULT_BUFFER_SIZE;
        for (int i = 0; i < mTmpBitmapCapacity; ++i) {
            mTmpBitmapBuffer[i] = new ASSBitmap();
        }
    } else {
        // Clear all the buffered entries
        for (int i = 0; i < mTmpBitmapCapacity; ++i) {
            mTmpBitmapBuffer[i]->clear();
        }
    }
    mTmpBitmapBuffer[0]->add(images);
    mTmpBitmapCount = 1;

    // Organize the the rest of the images to separate zones for blending later
    ASS_Image *curImg = images->next;
    while (curImg != nullptr) {
        // See if this image overlaps area with over images already in the list
        bool sorted = false;
        for (int i = 0; i < mTmpBitmapCount; ++i) {
            if (mTmpBitmapBuffer[i]->overlaps(curImg)) {
                mTmpBitmapBuffer[i]->add(curImg);
                sorted = true;
                break;
            }
        }
        if (!sorted) {
            // Did not overlap so add it to the list
            ensureTmpBufferCapacity(mTmpBitmapCount + 1);
            mTmpBitmapBuffer[mTmpBitmapCount]->add(curImg);
            mTmpBitmapCount++;
        }
        curImg = curImg->next;
    }

    // Check for the differences between this and last frame, move all bitmaps that did not change
    // and only blends new images if they have changed
    if (mBitmapBuffer && mBitmapCount > 0) {
        int oldImagesChanged = 0;

        // Try to match each old bitmaps with new bitmaps, check if there was any changes, mark it
        // changed and then add them to the list to remove
        for (size_t oldIndex = 0; oldIndex < mBitmapCount; ++oldIndex) {
            ASSBitmap* oldBitmap = mBitmapBuffer[oldIndex];
            if (oldBitmap->numOfImages == 0) {
                // At the partition of the list where these images were previously removed, end here
                break;
            }
            oldBitmap->changed = true;

            for (size_t newIndex = 0; newIndex < mTmpBitmapCount; ++newIndex) {
                ASSBitmap* newBitmap = mTmpBitmapBuffer[newIndex];
                if (newBitmap->changed && oldBitmap->compare(newBitmap) == 0) {
                    // Data matches, mark both as not changed, then swap the new and old bitmaps to
                    // carry on the original data (at the end the two lists are swapped)
                    newBitmap->size = 0;
                    oldBitmap->changed = newBitmap->changed = false;
                    mTmpBitmapBuffer[newIndex] = oldBitmap;
                    mBitmapBuffer[oldIndex] = newBitmap;
                    break;
                }
            }

            if (oldBitmap->changed) {
                oldImagesChanged++;
            }
        }

        // Append/move all old bitmaps that were changed as they were removed from this new frame
        if (oldImagesChanged > 0) {
            ensureTmpBufferCapacity(oldImagesChanged + mTmpBitmapCount);
            for (size_t i = 0; i < mBitmapCount; ++i) {
                ASSBitmap* bitmap = mBitmapBuffer[i];
                if (bitmap->numOfImages == 0) {
                    // Got to the removed images, we are done moving the images
                    break;
                } else if (bitmap->changed) {
                    // Move the changed bitmap to the temp list by swapping
                    bitmap->numOfImages = bitmap->stride = bitmap->size = 0;
                    mBitmapBuffer[i] = mTmpBitmapBuffer[mTmpBitmapCount];
                    mTmpBitmapBuffer[mTmpBitmapCount] = bitmap;
                    mTmpBitmapCount++;
                }
            }
        }
    }

    // Now that the images are organized, flatten all the changed bitmaps
    for (int i = 0; i < mTmpBitmapCount; ++i) {
        ASSBitmap* bitmap = mTmpBitmapBuffer[i];
        if (bitmap->changed && bitmap->numOfImages > 0) {
            bitmap->flattenImage();
        }
    }

    if (size) {
        *size = mTmpBitmapCount;
    }

    // Swap buffers to old last data and return it
    swapBuffers();

    return mBitmapBuffer;
}

void ASSRenderer::swapBuffers() {
    ASSBitmap** tmp = mTmpBitmapBuffer;
    mTmpBitmapBuffer = mBitmapBuffer;
    mBitmapBuffer = tmp;
    int tmpCount = mTmpBitmapCount;
    mTmpBitmapCount = mBitmapCount;
    mBitmapCount = tmpCount;
    int tmpCapacity = mTmpBitmapCapacity;
    mTmpBitmapCapacity = mBitmapCapacity;
    mBitmapCapacity = tmpCapacity;
}

void ASSRenderer::ensureTmpBufferCapacity(int size) {
    if (mTmpBitmapCapacity < size) {
        // Resize the buffer
        auto **buf = new ASSBitmap *[size + 2];
        int i = 0;

        // Copy data over
        for (; i < mTmpBitmapCapacity; ++i) {
            buf[i] = mTmpBitmapBuffer[i];
        }
        mTmpBitmapCapacity = size + 2;
        for (; i < mTmpBitmapCapacity; ++i) {
            buf[i] = new ASSBitmap();
        }
        delete[] mTmpBitmapBuffer;
        mTmpBitmapBuffer = buf;
    }
}
