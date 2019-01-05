#ifndef BITMAP_SECTION_H
#define BITMAP_SECTION_H

#include <ass/ass.h>
#include <vector>

class ASSBitmap {
public:
    ASSBitmap();
    ~ASSBitmap();

    void clear();

    bool overlaps(ASS_Image* image);

    void add(ASS_Image* image);

    void flattenImage();

    int compare(ASSBitmap* bitmap);

    static void blendSubtitle(uint8_t * buffer, size_t stride, ASS_Image *srcImage);

    uint8_t* buffer;
    size_t size;
    size_t stride;
    int x1;
    int x2;
    int y1;
    int y2;
    bool changed;
    size_t numOfImages;

    std::vector<ASS_Image> mImages;
private:
    size_t mBufferCapacity;
};

#endif //BITMAP_SECTION_H
