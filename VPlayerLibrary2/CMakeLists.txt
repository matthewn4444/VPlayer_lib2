# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

set(cpp_DIR ${CMAKE_SOURCE_DIR}/src/main/cpp)
set(ffmpeg_DIR ${cpp_DIR}/ffmpeg-build/${ANDROID_ABI})

add_library(lib_ffmpeg SHARED IMPORTED)
set_target_properties(lib_ffmpeg PROPERTIES IMPORTED_LOCATION
    ${ffmpeg_DIR}/libffmpeg.so)

include_directories(${ffmpeg_DIR}/include)


# Libyuv, used for 444 to argb conversion, ffmpeg's implementation is not accelerated
set( libyuv_DIR ${cpp_DIR}/thirdparty/libyuv )
set( libyuv_BDIR ${libyuv_DIR}/output )

file(MAKE_DIRECTORY ${libyuv_BDIR})
add_subdirectory(${libyuv_DIR} ${libyuv_BDIR})
add_library( lib_yuv STATIC IMPORTED )
set_target_properties( lib_yuv PROPERTIES IMPORTED_LOCATION
                       ${libyuv_BDIR}/libyuv.a )

include_directories( ${libyuv_DIR}/include )


add_definitions(-DCONFIG_RTSP_DEMUXER)

add_library(application SHARED
            src/main/cpp/player/StreamComponent.cpp
            src/main/cpp/player/AVComponentStream.cpp
            src/main/cpp/player/AvFramePool.cpp
            src/main/cpp/player/VideoStream.cpp
            src/main/cpp/player/AudioStream.cpp
            src/main/cpp/player/SubtitleFrameQueue.cpp
            src/main/cpp/player/SubtitleStream.cpp
            src/main/cpp/player/SSAHandler.cpp
            src/main/cpp/player/convert.cpp
            src/main/cpp/player/ImageSubHandler.cpp
            src/main/cpp/player/BasicYUVConverter.cpp
            src/main/cpp/player/YUV16to8Converter.cpp
            src/main/cpp/player/Frame.cpp
            src/main/cpp/player/FrameQueue.cpp
            src/main/cpp/player/PacketQueue.cpp
            src/main/cpp/player/Clock.cpp
            src/main/cpp/player/Player.cpp
            src/main/cpp/player/ASSRenderer.cpp
            src/main/cpp/player/ASSBitmap.cpp
            src/main/cpp/player/android/JniCallbackHandler.cpp
            src/main/cpp/player/android/JniVideoRenderer.cpp
            src/main/cpp/player/android/subtitles_jni.cpp
            src/main/cpp/player/android/JniHelper.cpp
            src/main/cpp/player/android/player_jni.cpp
            src/main/cpp/player/android/AudioRenderer.cpp
        )

target_link_libraries(application
                        android
                        lib_ffmpeg
                        lib_yuv
                        log )