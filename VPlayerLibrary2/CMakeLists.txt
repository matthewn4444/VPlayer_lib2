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

add_definitions(-DCONFIG_RTSP_DEMUXER)

add_library(application SHARED
             src/main/cpp/player/application.cpp
              src/main/cpp/player/JniCallbackHandler.cpp
              src/main/cpp/player/JniVideoRenderer.cpp
              src/main/cpp/player/AudioRenderer.cpp
              src/main/cpp/player/StreamComponent.cpp
              src/main/cpp/player/AVComponentStream.cpp
              src/main/cpp/player/AvFramePool.cpp
              src/main/cpp/player/VideoStream.cpp
              src/main/cpp/player/AudioStream.cpp
              src/main/cpp/player/SubtitleStream.cpp
              src/main/cpp/player/Frame.cpp
              src/main/cpp/player/FrameQueue.cpp
              src/main/cpp/player/PacketQueue.cpp
              src/main/cpp/player/Clock.cpp
              src/main/cpp/player/Player.cpp)

#target_include_directories(application PRIVATE
                         #${ffmpeg_DIR}/include)

target_link_libraries(application
                        android
                        lib_ffmpeg
                        log )