This page describes how to compile FFmpeg using NDK for Android. This is a more intermediate challenge.

This is **not a mandatory step** to integrate a basic video player into your application, to integrate **without compiling FFmpeg**, go [here](https://github.com/matthewn4444/VPlayer_lib/wiki/Compiling-VPlayer#building-vplayer-with-ffmpeg-binaries). However if you want to reduce the binary size or limit the codecs etc, this is will be useful.

Clang compiler is used by default and will only build arm (v7+) and x86 architecture and their respected 64 bit variants. mips and armv5 are depreciated by Android and can only be built with GCC. More info to build GCC is below.

## Prerequisites

### Tools

**Compiles only on Mac and Linux** (Windows would need work and there is no steps given here).

You need the following tools:

- autoconf
- autoconf-archive
- automake
- pkg-config
- git
- libtool
- yasm (for libjpeg turbo x86)
- nasm (for libjpeg turbo x86)
- texlive (for nettle)

**Command (Debian/Ubuntu):**

``sudo apt-get install autoconf autoconf-archive automake pkg-config git libtool yasm nasm texlive``

For mac: you have to install xcode and command tools from xcode preferences (tool brew from homebrew project)


**Download [Eclipse](https://developer.android.com/sdk/index.html)** and setup the environment for Android (or [Android Studio](https://developer.android.com/sdk/installing/studio.html), or use Ant if you want).

**Download any version of [NDK](https://developer.android.com/tools/sdk/ndk/index.html)** (can also build with the x64 variant and preferably new versions)

### Cloning the project

Clone the project:

``git clone https://github.com/matthewn4444/VPlayer_lib.git``

## Building Source and FFmpeg

_*Note: If you want to speed up the build process, limit the architectures and/or lessen the codecs.*_

1. Set the path to NDK: ``export NDK=~/<path to NDK>/NDK`` (you can store it in your ~/.bashrc file if you want) or use ``--ndk=<path>`` with *build_android.sh*

2. Go into the folder **{root}/VPlayer_library** and edit **build.gradle**:
   - Modify the first line of **compileSdkVersion** for which platform to build with (if you use a higher number, it will choose the next lower version; e.g you choose _android-10_, it will choose _android-9_ if 10 doesn't exist)
   - Modify the first line of **abiFilters** for which architectures to build for (armeabi, armeabi-v7a, arm64_6-v8a x86, x86_64 or mips)
   - Modify the first line of **arguments** (under externalNativeBuild -> ndkBuild) for *-j#* to specify number of jobs to build with

3. **[Optional]** Modify **build_android.sh** if you like to build with GCC, by default clang will build without any modifications. GCC is depreciated
and replaced by clang. Note that clang will not build depreciated architectures such as armv5 and mips (and 64bit mips).

2. Go into the folder **{root]/ffmpeg_build** and run **config_and_build.sh** once.
   - This will build with the newest toolchain from your NDK folder. If you want to use a specific version, modify it in the _android_android.sh_ file
   - If the build fails, you might want to run this again.
   - If the build succeeds and then if you want to build again, you only need to run **build_android.sh** (you do not need to configure again)

3. The output of the files are in **{root}/VPlayer_library/jni/ffmpeg-build/{arch}/libffmpeg.so**

## Customizing FFmpeg

Edit the file **{root}/ffmpeg_build/build_android.sh** under _function build_ffmpeg_

## Build Script Command Line

Both *build_android.sh* and *config_and_build.sh* now support command line argument interface. To see what the options are you can use

 ``build_android.sh --help``

``--ndk=<directory>``
If you do not want to export and environment variable for NDK, you can use this optional argument

``-j#``
Set the number of jobs. By default it will be 4 unless specified in build.gradle under the first arguments line in the file.

``-p=# or --platform=#``
Specify the platform SDK. By default is 9 unless specified in build.gradle under *compileSdkVersion*.

``-a=<list> or --arch=<list>``
Specify list of architectures used (mips, armeabi (both deprecated), arm64-v8a, x86, x86_64, armeabi-v7a). By default it will read from build.gradle first line that has *abiFilters*.

You can also use 2 special commands ``-a=all`` which will build everything with clang (except deprecated) and ``-a=all_with_deprecated`` to also build mips and armeabi with gcc.

``--gcc``
Compile all the architectures specified with gcc.

``--use-h264``
Will build and include h264 encoding in the shared library.

``--use-fdk-aac``
Will build and include fdk aac in the shared library.

``--no-subs``
Will not build or include subs in the shared library. By default it will build subs.

### Customize Architectures

Go to **{root}/VPlayer_library/build.gradle** and modify which architectures to change. _armeabi-v7a_ will build with neon. Each build will take a long time so try to reduce your architecture list. Try to increase the number of jobs that suits your pc to increase compile times.

### Customize Codecs

Starting from line 316 lists a bunch of configurations for FFmpeg. After **--disable-everything** you can disable enable certain codecs. [Here](http://ffmpeg.mplayerhq.hu/general.html) is some more information.

### Customize Subtitles

To reduce the file size of _libffmpeg.so_ you can build without subtitles if you don't need them.

Go to **{root}/VPlayer_library/jni/Android.mk** and comment out ``SUBTITLES=yes`` and it will not compile with subtitles. Likewise, uncomment the line to allow subtitles. You will save about 1-2mb off the shared library. Deciding to compile the NDK application with subtitles will lead to build errors when not compiling FFmpeg with subtitles. Use can also use the command line ``--no-subs`` to not compile and include subtitles.

### Customize Toolchain Version

Edit **{root}/ffmpeg_build/build_android.sh** by uncommenting ``TOOLCHAIN_VER=4.6`` and change number with the number available in your toolchain library **{path to ndk}/toolchains/**.

### Build with GCC instead of Clang

By default clang will be used because GCC is depreciated. Edit **{root}/ffmpeg_build/build_android.sh** by uncommenting ``USE_GCC=yes`` or use command line with ``--gcc``.

### Clang use specific location for standalone toolchain

By default standalone toolchain (only for clang) will be placed into ``/tmp/android-toolchain``. Customize this location by editing **{root}/ffmpeg_build/build_android.sh** and change ``/tmp/android-toolchain/`` to another location.

### Customize to use x264

If you do not need _libx264_ then edit **{root}/ffmpeg_build/build_android.sh** and comment ``ENABLE_X264=yes`` or use command line with ``--use-h264``.

### Customize AAC

You can either use vo-aacenc or fdk-aac. fdk-aac is the superior encoder however because of licensing, the prebuilt libraries I posted will not have them available, you will need to build FFmpeg yourself (luckily it is built by default).

Edit **{root}/ffmpeg_build/build_android.sh** by commenting ``PREFER_FDK_AAC=yes`` to use **vo-aacenc** or leave it uncommented to use **fdk-aac** or use command line ``--use-fdk-acc`` when running the script.

## Extra Stuff

This stuff is not needed for building FFmpeg, just extra information.

### What _*config_and_build.sh*_ does

On AndroidFFmpeg's readme, he gives some instructions of how to customize before building. The problem is that some of his instructions are out of date and would not work without extra Googling. This script does all of that so it makes this process easier. This is what it does:

1. ``git submodule update --init --recursive``

   Recursively updates all the submodules and downloads the other libraries (such as libass and FFmpeg)

2. ``sh ./autogen.sh`` & ``autoreconf -ivf``

   These two are used to configure the environments for freetype2, fribidi, libass, vo-aacenc and vo-amrwbenc

3. ``addAutomakeOpts``

   Adds ``1iAUTOMAKE_OPTIONS=subdir-objects`` to the Makefile.am files inside vo-aacenc and vo-amrwbenc because it needs them to configure successfully.

4. ``source build_android.sh``
   Starts the compilation of FFmpeg.