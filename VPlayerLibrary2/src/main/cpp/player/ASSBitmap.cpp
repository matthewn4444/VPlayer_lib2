#include "ASSBitmap.h"
#include <cmath>

ASSBitmap::ASSBitmap() :
        buffer(nullptr),
        size(0),
        mBufferCapacity(0),
        stride(0),
        numOfImages(0),
        changed(false),
        x1(0),
        x2(0),
        y1(0),
        y2(0) {
}

ASSBitmap::~ASSBitmap() {
    // Images are handled elsewhere and would be stale, only need to delete buffer
    if (buffer) {
        delete[] buffer;
        buffer = nullptr;
    }
}

void ASSBitmap::clear() {
    size = 0;
    stride = 0;
    changed = false;
    numOfImages = 0;
    x1 = 0;
    x2 = 0;
    y1 = 0;
    y2 = 0;
}

bool ASSBitmap::overlaps(ASS_Image *image) {
    return x1 < (image->dst_x + image->w) && x2 > image->dst_x &&
        y1 < (image->dst_y + image->h) && y2 > image->dst_y;
}

void ASSBitmap::add(ASS_Image *image) {
    if (numOfImages == 0) {
        // If no images, bounding box is first image
        x1 = image->dst_x;
        y1 = image->dst_y;
        x2 = image->dst_x + image->w;
        y2 = image->dst_y + image->h;
    } else {
        // If image overlaps, expand the bounding box
        x1 = std::min(x1, image->dst_x);
        y1 = std::min(y1, image->dst_y);
        x2 = std::max(x2, image->dst_x + image->w);
        y2 = std::max(y2, image->dst_y + image->h);
    }
    numOfImages++;

    if (mImages.size() < numOfImages) {
        mImages.emplace_back(ASS_Image());
    }

    // Copy the data into image buffer
    mImages[numOfImages - 1].dst_x = image->dst_x;
    mImages[numOfImages - 1].dst_y = image->dst_y;
    mImages[numOfImages - 1].w = image->w;
    mImages[numOfImages - 1].h = image->h;
    mImages[numOfImages - 1].stride = image->stride;
    mImages[numOfImages - 1].color = image->color;
    mImages[numOfImages - 1].bitmap = image->bitmap;
    mImages[numOfImages - 1].next = image->next;
    mImages[numOfImages - 1].type = image->type;
    changed = true;
}

void ASSBitmap::flattenImage() {
    // Prepare the buffer
    size = 0;
    stride = 0;
    const auto width = (size_t) (x2 - x1);
    // Align width to 8 bytes for assembly operations when blending
    const size_t newSize = (size_t) (ceil((float)width / 8.0f) * 8) * (y2 - y1) * 4;
    if (newSize <= 0) {
        // Cannot flatten with in valid size
        return;
    }
    stride = width * 4;
    size = newSize;
    if (size > mBufferCapacity) {
        // Requires resize
        delete[] buffer;
        buffer = new uint8_t[size];
        mBufferCapacity = size;
    }

    // Blend the images to a clear buffer
    memset(buffer, 0, size * sizeof(uint8_t));
    for (size_t i = 0; i < numOfImages; ++i) {
        ASS_Image& image = mImages[i];
        int xOffset = image.dst_x - x1;
        int yOffset = image.dst_y - y1;
        uint8_t* dst = buffer + xOffset * 4 + (yOffset * stride);
        blendSubtitle(dst, stride, &image);
    }
}

static int ass_image_compare(ASS_Image *i1, ASS_Image *i2)
{
    if (i1->w != i2->w)
        return 2;
    if (i1->h != i2->h)
        return 2;
    if (i1->stride != i2->stride)
        return 2;
    if (i1->color != i2->color)
        return 2;
    if (i1->bitmap != i2->bitmap)
        return 2;
    if (i1->dst_x != i2->dst_x)
        return 1;
    if (i1->dst_y != i2->dst_y)
        return 1;
    return 0;
}

int ASSBitmap::compare(ASSBitmap *bitmap) {
    if (numOfImages != bitmap->numOfImages || x1 != bitmap->x1 || x2 != bitmap->x2
            || y1 != bitmap->y1 || y2 != bitmap->y2) {
        return 2;
    }
    int ret = 0;
    for (size_t i = 0; i < numOfImages; ++i) {
        int result = ass_image_compare(&mImages[i], &bitmap->mImages[i]);
        if (result >= 2) {
            return result;
        }
        ret = std::max(result, ret);
    }
    return ret;
}

void ASSBitmap::blendSubtitle(uint8_t *buffer, size_t stride, ASS_Image *srcImage) {
    const uint8_t* dst = buffer;
    const uint8_t* src = srcImage->bitmap;
    const auto width = (size_t) srcImage->w;
    const auto height = (size_t) srcImage->h;

    // Calculate the distance between the end of one line to the start of the next
    // libass stride is per pixel while buffer stride is by bytes
    const auto srcStrideOffset = (ptrdiff_t) (srcImage->stride - ceil(width / 8.0) * 8);
    const auto dstStrideOffset = (ptrdiff_t) (stride - (ceil(width / 8.0) * 8) * 4);

#if __aarch64__
    asm volatile (
        // Read color list [ABGR], load to vectors and inverse alpha
        "ld4r           { v2.8b - v5.8b }, [%0]             \n"

        // Load 8 slots of 0xFF
        "mov            w0, #0xFF                           \n"
        "dup            v0.8b, w0                           \n"
        "sub            v2.8b, v0.8b, v2.8b                 \n"

        // For loop, count down the width
        "1:                                                 \n"
        "mov            %0, %3                              \n"

        // Load the source and destination data [BGRA]
        "2:                                                 \n"
        "ld1            { v6.8b }, [%1], #8                 \n"
        "ld4            { v7.8b - v10.8b }, [%2]            \n"

        // Prepare the source alpha
        "and            v11.8b, v6.8b, v2.8b                \n"     // Source alpha
        "sub            v12.8b, v0.8b, v11.8b               \n"     // Source alpha inverse

        // Compute alpha pixel data
        "and            v1.8b, v6.8b, v2.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"     // v = source * alpha
        "umlal          v1.8h, v12.8b, v10.8b               \n"     // v = v + (bg * alphaInv)
        "uqrshrn        v10.8b, v1.8h, #8                   \n"     // v = v >> 8 or v = v / 256

        // Compute blue pixel data
        "and            v1.8b, v6.8b, v3.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v9.8b                \n"
        "uqrshrn        v9.8b, v1.8h, #8                    \n"

        // Compute green pixel data
        "and            v1.8b, v6.8b, v4.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v8.8b                \n"
        "uqrshrn        v8.8b, v1.8h, #8                    \n"

        // Compute red pixel data
        "and            v1.8b, v6.8b, v5.8b                 \n"
        "umull          v1.8h, v1.8b, v11.8b                \n"
        "umlal          v1.8h, v12.8b, v7.8b                \n"
        "uqrshrn        v7.8b, v1.8h, #8                    \n"

        // Write back to dst
        "st4            { v7.8b - v10.8b }, [%2], #32       \n"

        // End loop, x = width; x > 0; x -= 8
        "subs           %0, %0, #8                          \n"
        "b.gt           2b                                  \n"

        // End loop, y = height; y > 0; y--
        // Add the offset to src/dst to get the next pixel
        "add            %1, %1, %5                          \n"
        "add            %2, %2, %6                          \n"
        "subs           %4, %4, #1                          \n"
        "b.gt           1b                                  \n"
    :
    :   "r" (&srcImage->color),
        "r" (src),
        "r" (dst),
        "r" (width),
        "r" (height),
        "r" (srcStrideOffset) ,
        "r" (dstStrideOffset)
    :   "memory", "cc",
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12"
    );
#elif __ARM_NEON__
    asm volatile (
        // Load 8 slots of 0rFF
        "mov            r0, #0xFF                           \n"
        "vdup.8         d2, r0                              \n"

        // Read color list [ABGR], load to vectors and inverse alpha
        "vld4.8         { d3[] - d6[] }, [%0]               \n"
        "vsub.u8        d3, d2, d3                          \n"

        // For loop, count down the width
        "1:                                                 \n"

        "mov            r0, %3                              \n"

        // Load the source and destination data
        "2:                                                 \n"
        "vld1.8         { d7 }, [%1]!                       \n"
        "vld4.8         { d8 - d11 }, [%2]                  \n"

        // Prepare the source alpha
        "vand.u8        d12, d7, d3                         \n"     // Source alpha
        "vsub.u8        d13, d2, d12                        \n"     // Source alpha inverse

        // Compute alpha pixel data
        "vand.u8        d1, d7, d3                          \n"
        "vmull.u8       q0, d1, d12                         \n"     // v = source * alpha
        "vmlal.u8       q0, d13, d11                        \n"     // v = v + (bg * alphaInv)
        "vqrshrn.u16    d11, q0, #8                         \n"     // v = v >> 8 or v = v / 256

        // Compute blue pixel data
        "vand.u8        d1, d7, d4                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d10                        \n"
        "vqrshrn.u16    d10, q0, #8                         \n"

        // Compute green pixel data
        "vand.u8        d1, d7, d5                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d9                         \n"
        "vqrshrn.u16    d9, q0, #8                          \n"

         // Compute red pixel data
        "vand.u8        d1, d7, d6                          \n"
        "vmull.u8       q0, d1, d12                         \n"
        "vmlal.u8       q0, d13, d8                         \n"
        "vqrshrn.u16    d8, q0, #8                          \n"

        // Write back to dst
        "vst4.8         { d8 - d11 }, [%2]!                 \n"

        // End loop, x = width; x > 0; x -= 8
        "subs           r0, r0, #8                          \n"
        "bgt            2b                                  \n"

        // End loop, y = height; y > 0; y--
        // Add the offset to src/dst to get the next pixel
        "add            %1, %1, %5                          \n"
        "add            %2, %2, %6                          \n"
        "subs           %4, %4, #1                          \n"
        "bgt            1b                                  \n"
    :
    :   "r" (&srcImage->color),
        "r" (src),
        "r" (dst),
        "r" (width),
        "r" (height),
        "r" (srcStrideOffset),
        "r" (dstStrideOffset)
    :   "memory", "cc", "r0",
        "d0", "d1", "d2", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12", "d13"
    );
#else
    const uint8_t rgba_color[] = {
            (uint8_t)(srcImage->color >> 24),               // R  0
            (uint8_t) ((srcImage->color >> 16) & 0xFF),     // G  1
            (uint8_t) ((srcImage->color >> 8) & 0xFF),      // B  2
            (uint8_t) (0xFF - (srcImage->color & 0xFF))     // A  3
    };

    uint8_t srcR, srcG, srcB, srcA;
    size_t dstR, dstG, dstB, dstA;
    size_t x, y;

    for (y = height; y > 0; --y) {
        const uint8_t* _src = src;
        uint32_t* _dst = (uint32_t *) dst;

        for (x = width; x > 0; --x) {
            const uint8_t srcPixel = *(_src++);
            uint32_t *dstPixel = _dst++;

            // Expand the subtitle pixel
            srcR = srcPixel & rgba_color[0];
            srcG = srcPixel & rgba_color[1];
            srcB = srcPixel & rgba_color[2];
            srcA = srcPixel & rgba_color[3];

            dstA = (dstPixel[0] >> 24) & 0xff;
            dstB = (dstPixel[0] >> 16) & 0xff;
            dstG = (dstPixel[0] >> 8) & 0xff;
            dstR = dstPixel[0] & 0xff;

            // Pixel blending
	        dstA = (((dstA * (0xff - srcA)) + (srcA * srcA))/0xff);
	        dstR = (((dstR * (0xff - srcA)) + (srcR * srcA))/0xff);
            dstG = (((dstG * (0xff - srcA)) + (srcG * srcA))/0xff);
            dstB = (((dstB * (0xff - srcA)) + (srcB * srcA))/0xff);

            // Write pixel back to frame
            dstPixel[0] = (uint32_t) ((dstA << 24) | (dstB << 16) | (dstG << 8) | dstR);
        }
        src += srcImage->stride;
        dst += stride;
    }
#endif
}
