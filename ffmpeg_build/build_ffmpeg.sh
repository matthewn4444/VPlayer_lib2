#!/bin/bash
# =======================================================================
#   Customize FFmpeg build
#       Comment out what you do not need, specifying 'no' will not
#       disable them.
#
#   Building with x264
#       This will upgrade the license to GPL
# ENABLE_X264=yes

#   Toolchain version
#       Comment out if you want default or specify a version
#       Default takes the highest toolchain version from NDK
# TOOLCHAIN_VER=4.6

#   Use fdk-aac
#       Default uses FFMPEG's aac and fdk-aac could be better but
#       because of licensing, it requires you to build FFmpeg from
#       scratch if you want to use fdk-aac. Uncomment to use fdk-aac
#       This will upgrade the license to GPL
# ENABLE_FDK_AAC=yes

#   Opencore AMR
#       AMR-NB decoding/encoding and AMR-WB decoding which also includes a AAC decoder/encoder
#       FFMPEG comes with an AAC decoder but if you need another one you can use this
#       This will upgrade the license to GPL
# ENABLE_AMR=yes

#   Specify Toolchain root
#       https://developer.android.com/ndk/guides/standalone_toolchain.html#creating_the_toolchain
TOOLCHAIN_ROOT=/tmp/android-toolchain/

#   Include libjpeg-turbo in build
#       Jpeg is only needed for the application not shared library for libyuv,
#       If you need libjpeg-turbo inside the shared library then uncomment.
#       It is not included to save space in the binary.
#INCLUDE_JPEG=yes

#   Enable FFMPEG Encoders
#       Quick way to enable all decoders and muxers, by default is off for video playback
# ENABLE_ENCODING=yes

#   Enable FontConfig
#       Allow the FFMPEG and libass to use commands from fontconfig library.
# ENABLE_FONTCONFIG=yes

#   Enable Subtitles
#       Disable to remove subtitles from the video
BUILD_WITH_SUBS=yes

#
# =======================================================================

# Parse command line
for i in "$@"
do
case $i in
    -j*)
    JOBS="${i#*j}"
    shift
    ;;
    -a=*|--arch=*)
    BUILD_ARCHS="${i#*=}"
    if [[ " ${BUILD_ARCHS[*]} " == *" all_with_deprecated "* ]]; then
        BUILD_ALL_WITH_DEPS=true
        BUILD_ALL=true
    fi
    shift
    ;;
    --use-h264)
    ENABLE_X264=yes
    shift
    ;;
    --use-amr)
    ENABLE_AMR=yes
    shift
    ;;
    --use-fontconfig)
    ENABLE_FONTCONFIG=yes
    shift
    ;;
    --enable-encoding)
    ENABLE_ENCODING=yes
    shift
    ;;
    --use-fdk-aac)
    ENABLE_FDK_AAC=yes
    shift
    ;;
    -p=*|--platform=*)
    PLATFORM_VERSION="${i#*=}"
    shift
    ;;
    --no-subs)
    BUILD_WITH_SUBS=no
    shift
    ;;
    --ndk=*)
    eval NDK="${i#*=}"
    shift
    ;;
    -h|--help)
    echo "Usage: build_ffmpeg.sh [options]"
    echo "  Options here will override options from files it may read from"
    echo
    echo "Help options:"
    echo "  -h, --help                  displays this message and exits"
    echo
    echo "Building library options:"
    echo "  --use-h264                  build with h264 encoding library, uses GPL license"
    echo "  --use-amr                   build with Opencore AMR-NB/WB decoder/encoder, uses GPL license"
    echo "  --use-fdk-aac               build with fdk acc instead of ffmpeg's aac, uses GPL license"
    echo "  --use-fontconfig            build with fontconfig used by FFMPEG and libass"
    echo "  --no-subs                   do not build with subs"
    echo
    echo "Optional build flags:"
    echo "  -j#[4]                      number of jobs, default is 4 (threads)"
    echo "                              this will override the setting in ../VPlayerLibrary2/build.gradle"
    echo "                              'android.defaultConfig.externalNativeBuild.ndkBuild.arguments' line"
    echo "  -p=[21], --platform=[21]    build with sdk platform"
    echo "                              this will override the setting in ../VPlayerLibrary2/build.gradle"
    echo "                              'android.compileSdkVersion'"
    echo "  --ndk=[DIR]                 path to your ndk and will override the environment variable"
    echo "  -a=[LIST], --arch=[LIST]    enter a list of architectures to build with"
    echo "                              this will override the setting in ../VPlayerLibrary2/build.gradle"
    echo "                              of the first match of line 'abiFilters'"
    echo "                              options include arm64-v8a, x86, x86_64, armeabi-v7a"
    echo "                              'all' would built allarchitectures using clang"
    exit 1
    shift
    ;;
    *)
    echo "Warning: unknown argument '${i#*=}'"
    ;;
esac
done

# Go to the folder of the script if not already to fix paths
cd "$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Check environment for ndk build
if [ -f "$NDK/ndk-build" ]; then
    NDK="$NDK"
elif [ -z "$NDK" ]; then
    echo NDK variable not set or in path, exiting
    echo "   Example: export NDK=/your/path/to/android-ndk"
    echo "   Or add your ndk path to ~/.bashrc"
    echo "   Or use --ndk=<path> with command"
    echo "   Then run ./build_ffmpeg.sh"
    exit 1
fi

# Read the build.gradle for default inputs
while read line; do
    # Parse the platform sdk version
    if [ -z "$PLATFORM_VERSION" ] && [[ $line = compileSdkVersion* ]]; then
        a=${line##compileSdkVersion}
        PLATFORM_VERSION=`echo $a | sed 's/\\r//g'`
    fi

    # Parse for the architectures
    if [ -z "$BUILD_ARCHS" ] && [[ $line = abiFilters* ]]; then
        a=${line##abiFilters}
        BUILD_ARCHS=`echo ${a%%//,*} | sed 's/\*[^]]*\*//g'| sed "s/'//g"| sed "s/\"//g"| sed "s/\///g"`
    fi

    # Read jobs
    if [ -z "$JOBS" ] && [[ $line = arguments* ]]; then
        JOBS=`echo $line | sed 's/.*-j\([0-9]*\).*/\1/'`
    fi
done <"../VPlayerLibrary2/build.gradle"

# Default jobs
if [ -z "$JOBS" ]; then
    JOBS=4
fi

# Check if architectures are specified
if [ -z "$BUILD_ARCHS" ]; then
    echo "build.gradle has not specified any architectures, please use 'abiFilters'"
    exit 1
else
    BUILD_ARCHS=`echo $BUILD_ARCHS | sed "s/,/ /g" | sed 's/\\r//g'`
    if [[ " ${BUILD_ARCHS[*]} " == *" all "* ]]; then
        BUILD_ALL=true
    fi
    # Check for architecture inputs are correct
    if [ -z $BUILD_ALL ]; then
        if [[ " ${BUILD_ARCHS[*]} " != *" armeabi-v7a "* ]] \
               && [[ " ${BUILD_ARCHS[*]} " != *" armeabi "* ]] \
               && [[ " ${BUILD_ARCHS[*]} " != *" mips "* ]] \
               && [[ " ${BUILD_ARCHS[*]} " != *" x86 "* ]] \
               && [[ " ${BUILD_ARCHS[*]} " != *" x86_64 "* ]] \
               && [[ " ${BUILD_ARCHS[*]} " != *" arm64-v8a "* ]]; then
           echo "Cannot build with invalid input architectures: ${BUILD_ARCHS[@]}"
           exit
       fi
    fi
    echo "Building for the following architectures: "${BUILD_ARCHS[@]}
fi

# Further parse the platform version
if [ -z "$PLATFORM_VERSION" ]; then
    PLATFORM_VERSION=21      # Default
fi
if [ ! -d "$NDK/platforms/android-$PLATFORM_VERSION" ]; then
    echo "Android platform doesn't exist, try to find a lower version than" $PLATFORM_VERSION
    while [ $PLATFORM_VERSION -gt 0 ]; do
        if [ -d "$NDK/platforms/android-$PLATFORM_VERSION" ]; then
            break
        fi
        let PLATFORM_VERSION=PLATFORM_VERSION-1
    done
    if [ ! -d "$NDK/platforms/android-$PLATFORM_VERSION" ]; then
        echo Cannot find any valid Android platforms inside $NDK/platforms/
        exit 1
    fi
fi
echo Using Android platform from $NDK/platforms/android-$PLATFORM_VERSION

# Get the newest arm-linux-androideabi version
if [ -z "$TOOLCHAIN_VER" ]; then
    folders=$NDK/toolchains/arm-linux-androideabi-*
    for i in $folders; do
        n=${i#*$NDK/toolchains/arm-linux-androideabi-}
        reg='.*?[a-zA-Z].*?'
        if ! [[ $n =~ $reg ]] ; then
            TOOLCHAIN_VER=$n
        fi
    done
    if [ ! -d $NDK/toolchains/arm-linux-androideabi-$TOOLCHAIN_VER ]; then
        echo $NDK/toolchains/arm-linux-androideabi-$TOOLCHAIN_VER does not exist
        exit 1
    fi
fi
echo Using $NDK/toolchains/{ARCH}-$TOOLCHAIN_VER
echo "Compile with clang (standalone toolchain)"
# If using clang, check to see if there is gas-preprocessor.pl avaliable, this will require sudo!
GAS_PREPRO_PATH="/usr/local/bin/gas-preprocessor.pl"
if [ ! -x "$GAS_PREPRO_PATH" ]; then
    echo "Downloading needed gas-preprocessor.pl for FFMPEG"
    wget --no-check-certificate https://raw.githubusercontent.com/FFmpeg/gas-preprocessor/master/gas-preprocessor.pl
    chmod +x gas-preprocessor.pl
    mv gas-preprocessor.pl $GAS_PREPRO_PATH
    if  [ ! -x "$GAS_PREPRO_PATH" ]; then
        echo "  Cannot move file, please run this script with permissions [ sudo -E ./build_ffmpeg.sh ]"
        exit 1
    fi
    echo "  Finished downloading gas-preprocessor.pl"
fi

OS=`uname -s | tr '[A-Z]' '[a-z]'`

function replace_line
{
    sed -i "s/^$1/$2/" "$3"
}

function comment_line
{
    sed -i "s/^$1/\/\/$1/" "$2"
}

# Runs routines to find folders and links once per architecture
function setup
{
    export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig/
    export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig/
    PREBUILT=$TOOLCHAIN_ROOT
    PLATFORM=$NDK/platforms/android-$PLATFORM_VERSION/arch-$ARCH/
    export PATH=${PATH}:$PREBUILT/bin/
    CROSS_COMPILE=$PREBUILT/bin/$EABIARCH-

    # Changes in NDK leads to new folder paths, add them if they exist
    # https://android.googlesource.com/platform/ndk.git/+/master/docs/UnifiedHeaders.md
    if [ -d "$TOOLCHAIN_ROOT/sysroot" ]; then
        SYSROOT=$TOOLCHAIN_ROOT/sysroot
    else
        SYSROOT=$PLATFORM
    fi
    if [ -d "$SYSROOT/usr/include/$EABIARCH/" ]; then
        OPTIMIZE_CFLAGS=$OPTIMIZE_CFLAGS" -isystem $SYSROOT/usr/include/$EABIARCH/ -D__ANDROID_API__=$PLATFORM_VERSION -I$PREFIX/include"
    fi

    # Find libgcc.a to merge and link all the libraries
    LIBGCC_PATH=
    folders=$PREBUILT/lib/gcc/$EABIARCH/$TOOLCHAIN_VER*
    for i in $folders; do
        # If folder in standalone toolchain has no files, delete it (needed for gmp arm clang)
        if [ ! -n "$(ls -A $i)" ]; then
            echo "Deleting empty folder in toolchain because it messes up with builds: $i"
            rm -rf $i
        fi
        if [ -f "$i/libgcc.a" ]; then
            LIBGCC_PATH="$i/libgcc.a"
            break
        fi
    done
    if [ -z "$LIBGCC_PATH" ]; then
        echo "Failed: Unable to find libgcc.a from toolchain path, file a bug or look for it"
        exit 1
    fi

    # Link the GCC library if arm below and including armv7 TODO TEST IF NEEDED ANYMORE
    LIBGCC_LINK=
    if [[ $HOST == *"arm"* ]]; then
        LIBGCC_LINK="-l$LIBGCC_PATH"
    else
        LIBGCC_LINK="-lgcc"
    fi

    # Handle 64bit paths
    ARCH_BITS=
    if [[ "$ARCH" == *64 ]]; then
        ARCH_BITS=64
    fi

    # Find the library link folder
    LINKER_FOLDER=$SYSROOT/usr/lib
    if [ -d "$LINKER_FOLDER$ARCH_BITS" ]; then
        LINKER_FOLDER=$LINKER_FOLDER$ARCH_BITS
    fi

    # nostdlib flag remove donly on
    if [ "$CPU" != "armv7-a" ]; then
        LDFLAGS_EXTRA=-nostdlib
    else
        LDFLAGS_EXTRA=
    fi

    LINKER_LIBS=
    CFLAGS=$OPTIMIZE_CFLAGS
    export LDFLAGS="-Wl,-rpath-link=$LINKER_FOLDER -L$LINKER_FOLDER $LIBGCC_LINK $LDFLAGS_EXTRA -lc -lm -ldl -L$PREFIX/lib"
    export CPPFLAGS="$CFLAGS"
    export CFLAGS="$CFLAGS"
    export CXXFLAGS="$CFLAGS"
    export CXX="clang++"
    export AS="clang"
    export CC="clang"
    export NM="${CROSS_COMPILE}nm"
    export STRIP="${CROSS_COMPILE}strip"
    export RANLIB="${CROSS_COMPILE}ranlib"
    export AR="${CROSS_COMPILE}ar"
    export LD="${CROSS_COMPILE}ld"
}
function build_x264
{
    find x264/ -name "*.o" -type f -delete
    if [ ! -z "$ENABLE_X264" ]; then
        ADDITIONAL_CONFIGURE_FLAG="$ADDITIONAL_CONFIGURE_FLAG --enable-gpl --enable-libx264"
        LINKER_LIBS="$LINKER_LIBS -lx264"
        ENABLE_ENCODING=yes
        cd x264
        ./configure --prefix=$PREFIX --disable-gpac --host=$HOST --enable-pic --enable-static $ADDITIONAL_CONFIGURE_FLAG || exit 1
        make clean || exit 1
        make STRIP= -j${JOBS} install || exit 1
        cd ..
    fi
}
function build_amr
{
    if [ ! -z "$ENABLE_AMR" ]; then
        LINKER_LIBS="$LINKER_LIBS -lopencore-amrnb -lopencore-amrwb"
        cd opencore-amr-code
        ADDITIONAL_CONFIGURE_FLAG="$ADDITIONAL_CONFIGURE_FLAG --enable-libopencore-amrnb --enable-libopencore-amrwb --enable-version3"
        ./configure \
            --prefix=$PREFIX \
            --host=$HOST \
            --disable-dependency-tracking \
            --disable-shared \
            --enable-static \
            --with-pic \
            $ADDITIONAL_CONFIGURE_FLAG \
            || exit 1
        make clean || exit 1
        make -j${JOBS} install || exit 1
        cd ..
    fi
}
function build_fdk_aac
{
    if [ ! -z "$ENABLE_FDK_AAC" ]; then
        echo "Using fdk-aac encoder for AAC"
        ADDITIONAL_CONFIGURE_FLAG="$ADDITIONAL_CONFIGURE_FLAG --enable-libfdk_aac --enable-nonfree"
        LINKER_LIBS="$LINKER_LIBS -lfdk-aac"
        cd fdk-aac
        ./configure \
            --prefix=$PREFIX \
            --host=$HOST \
            --disable-dependency-tracking \
            --disable-shared \
            --enable-static \
            --with-pic \
            $ADDITIONAL_CONFIGURE_FLAG \
            || exit 1
        make clean || exit 1
        make -j${JOBS} install || exit 1
        cd ..
    fi
}
function build_jpeg
{
    if [ ! -z "$INCLUDE_JPEG" ]; then
        LINKER_LIBS="$LINKER_LIBS -ljpeg"
    fi
    cd libjpeg-turbo
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --disable-shared \
        --enable-static \
        --with-pic \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}
function build_png
{
    LINKER_LIBS="$LINKER_LIBS -lpng"
    cd libpng
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --disable-shared \
        --enable-static \
        --with-pic \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}
function build_freetype2
{
    ADDITIONAL_CONFIGURE_FLAG=$ADDITIONAL_CONFIGURE_FLAG" --enable-libfreetype"
    LINKER_LIBS="$LINKER_LIBS -lfreetype"
    cd freetype2
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --disable-require-system-font-provider \
        --disable-shared \
        --enable-static \
        --with-pic \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1
    make clean || exit 1
    make -j${JOBS} || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}
function build_ass
{
    EXTRA_LIBASS_FLAGS=
    if [ -z "$ENABLE_FONTCONFIG" ]; then
        EXTRA_LIBASS_FLAGS=" --disable-fontconfig --disable-require-system-font-provider"
    fi

    LINKER_LIBS="$LINKER_LIBS -lass"
    ADDITIONAL_CONFIGURE_FLAG=$ADDITIONAL_CONFIGURE_FLAG" --enable-libass"
    cd libass
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --disable-dependency-tracking \
        --disable-shared \
        --enable-static \
        --with-pic \
        $EXTRA_LIBASS_FLAGS \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1
    make clean || exit 1
    make V=1 -j${JOBS} install || exit 1
    cd ..
}
function build_fribidi
{
    export PATH=${PATH}:$PREBUILT/bin/
    LINKER_LIBS="$LINKER_LIBS -lfribidi"
    cd fribidi
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --with-glib=no \
        --disable-docs \
        --disable-dependency-tracking \
        --disable-shared \
        --enable-static \
        --with-pic \
        || exit 1
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}

function build_libuuid
{
    cd util-linux

    # Android doesn't have a temp folder, doubt this will hit anyways, so use sdcard
    replace_line '		tmpenv = _PATH_TMP' '		tmpenv = "\/sdcard\/"' "lib/fileutils.c"
    LINKER_LIBS="$LINKER_LIBS -luuid"
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --with-sysroot=$SYSROOT \
        --disable-dependency-tracking \
        --disable-shared \
        --disable-all-programs \
        --enable-libuuid \
        --enable-static \
        --with-pic \
        || exit 1
    make clean || exit 1
    make -j${JOBS} install || exit 1
    replace_line '		tmpenv = "\/sdcard\/"' '		tmpenv = _PATH_TMP' "lib/fileutils.c"
    cd ..
}

function build_libxml2
{
    export PATH=${PATH}:$PREBUILT/bin/
    PKG_CONFIG=${CROSS_COMPILE}pkg-config
    LINKER_LIBS="$LINKER_LIBS -lxml2"
    cd libxml2
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --without-python \
        --with-sysroot=$SYSROOT \
        --disable-shared \
        --enable-static \
        --with-pic \
        || exit 1
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}

function build_fontconfig
{
    if [ ! -z "$ENABLE_FONTCONFIG" ]; then
        build_libuuid
        build_libxml2

        export PATH=${PATH}:$PREBUILT/bin/
        PKG_CONFIG=${CROSS_COMPILE}pkg-config
        ADDITIONAL_CONFIGURE_FLAG=$ADDITIONAL_CONFIGURE_FLAG" --enable-libfontconfig"
        LINKER_LIBS="$LINKER_LIBS -lfontconfig"
        cd fontconfig
        ./configure \
            --prefix=$PREFIX \
            --host=$HOST \
            --build=$ARCH-unknown-linux-gnu \
            --disable-dependency-tracking \
            --with-sysroot=$SYSROOT \
            --enable-libxml2 \
            --disable-docs \
            --disable-nls \
            --disable-shared \
            --enable-static \
            --with-pic \
            || exit 1
        replace_line '	conf.d its po po-conf test $(am__append_1)' '	conf.d its po po-conf $(am__append_1)' Makefile
        make clean || exit 1
        make -j${JOBS} install || exit 1        # TODO do we need second make?
        replace_line '	conf.d its po po-conf $(am__append_1)' '	conf.d its po po-conf test $(am__append_1)' Makefile
        cd ..
    fi
}

function build_gnutls
{
    # Remove text relocations
    GMP_ASM_FLAG=
    if [ "$PLATFORM_VERSION" -ge "24" ]; then
        if [ "$ARCH" == "arm" ] || [ "$ARCH" == "x86_64" ]; then
            GMP_ASM_FLAG="--disable-assembly"
        fi
    fi

    # Compile dependency gmp
    cd gmp
    make distclean
    export CC_FOR_BUILD="/usr/bin/gcc"
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --enable-static \
        --disable-shared \
        $GMP_ASM_FLAG \
        || exit 1
    make -j${JOBS} install || exit 1
    cd ..

    # Compile dependency nettle
    cd nettle
    make distclean
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --disable-shared \
        --disable-openssl \
        --enable-static \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1

    # Only interested in compiling the library, do not need tools or tests
    replace_line 'SUBDIRS =.*' 'SUBDIRS =' Makefile
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..

    LINKER_LIBS="$LINKER_LIBS -lgnutls -lnettle -lhogweed -lgmp"
    ADDITIONAL_CONFIGURE_FLAG="$ADDITIONAL_CONFIGURE_FLAG --enable-gnutls"
    cd gnutls
    ./configure \
        --prefix=$PREFIX \
        --host=$HOST \
        --build=$ARCH-unknown-linux-gnu \
        --disable-dependency-tracking \
        --disable-tools \
        --disable-doc \
        --disable-tests \
        --disable-shared \
        --disable-cxx \
        --enable-static \
        --with-included-libtasn1 \
        --with-included-unistring \
        --without-p11-kit \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1

    # Static link, remove shared config so it can compile
    comment_line '#define HAVE___REGISTER_ATFORK 1' config.h

    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
}
function build_ffmpeg
{
    PKG_CONFIG=${CROSS_COMPILE}pkg-config
    if [ ! -f $PKG_CONFIG ];
    then
        cat > $PKG_CONFIG << EOF
#!/bin/bash
pkg-config \$*
EOF
        chmod u+x $PKG_CONFIG
    fi
    cd ffmpeg
    if [ -z "$ENABLE_ENCODING" ]; then
        ENCODING_OPT="--disable-encoders --disable-muxers"
    fi

    # Apparently order matters for gnutls
    if [ "$HOST" == "aarch64-linux" ]; then
        EXTRA_LIBS="$LIBGCC_LINK $LINKER_LIBS"
    else
        EXTRA_LIBS="$LINKER_LIBS $LIBGCC_LINK"
    fi

    echo "Configuring FFMPEG, this will take some time depending on options..."
    ./configure --target-os=linux \
        --prefix=$PREFIX \
        --enable-cross-compile \
        --arch=$ARCH \
        --cc=$CC \
        --nm=$NM \
        --sysroot=$SYSROOT \
        --extra-libs="$EXTRA_LIBS" \
        --extra-cflags=" -O3 -DANDROID -fpic -DHAVE_SYS_UIO_H=1 -Dipv6mr_interface=ipv6mr_ifindex -fasm -Wno-psabi -fno-short-enums  -fno-strict-aliasing -finline-limit=300 -I$PREFIX/include $OPTIMIZE_CFLAGS" \
        --disable-shared \
        --enable-static \
        --extra-ldflags="-Wl,-rpath-link=$SYSROOT/usr/lib -L$SYSROOT/usr/lib -nostdlib -lc -lm -lz -ldl -llog" \
        --disable-devices \
        --disable-doc \
        --disable-programs \
        --disable-avdevice \
        --disable-linux-perf \
        $ENCODING_OPT \
        $ADDITIONAL_CONFIGURE_FLAG \
        || exit 1
#        try  --disable-runtime-cpudetect if it plays?
#        try  --enable-small
    make clean || exit 1
    make -j${JOBS} install || exit 1
    cd ..
    LINKER_LIBS="$LINKER_LIBS -lavcodec -lavformat -lavutil -lswresample -lswscale"
}

function build_one {
    cd ffmpeg

    # Link all libraries into one shared object
    ${LD} -rpath-link=$LINKER_FOLDER -L$LINKER_FOLDER -L$PREFIX/lib  -soname $SONAME -shared -nostdlib -Bsymbolic \
    --whole-archive --no-undefined -o $OUT_LIBRARY $LINKER_LIBS -lc -lm -lz -ldl -llog \
    --dynamic-linker=/system/bin/linker -zmuldefs $LIBGCC_PATH || exit 1
    $PREBUILT/bin/$EABIARCH-strip --strip-unneeded $OUT_LIBRARY
    cd ..
}
function build_subtitles
{
    if [[ "$BUILD_WITH_SUBS" == "yes" ]]; then
        build_fribidi
        build_png
        build_freetype2
        build_fontconfig
        build_ass
    fi
}
function build
{
    echo "================================================================"
    echo "================================================================"
    echo "                      Building $ARCH"
    echo "$OUT_LIBRARY"
    echo "================================================================"
    echo "================================================================"
    if [ ! -z "$TOOLCHAIN_ROOT" ] && [ ! -d "$TOOLCHAIN_ROOT/$EABIARCH" ]; then
        echo "Creating standalone toolchain in $TOOLCHAIN_ROOT"
        $NDK/build/tools/make_standalone_toolchain.py --arch "$ARCH" --api $PLATFORM_VERSION --stl=libc++ --install-dir $TOOLCHAIN_ROOT --force
        echo "      Built the standalone toolchain"
    fi
    if [ -z "$HOST" ]; then
        HOST=$ARCH-linux
    fi
    setup
    build_x264
    build_amr
    build_fdk_aac
    build_subtitles
    build_jpeg
    build_gnutls
    build_ffmpeg
    build_one
    echo "Successfully built $ARCH"
    HOST=
}

# Delete the distributed folder in library
if [ -d "../VPlayerLibrary2/src/main/cpp/dist" ]; then
    echo "Deleting old binaries from dist folder in library"
    rm -rf ../VPlayerLibrary2/src/main/cpp/dist
fi

#x86
if [[ " ${BUILD_ARCHS[*]} " == *" x86 "* ]] || [ "$BUILD_ALL" = true ]; then
EABIARCH=i686-linux-android
ARCH=x86
OPTIMIZE_CFLAGS="-m32"
PREFIX=$(pwd)/../VPlayerLibrary2/src/main/cpp/ffmpeg-build/$ARCH
OUT_LIBRARY=$PREFIX/libffmpeg.so
ADDITIONAL_CONFIGURE_FLAG=--disable-asm
SONAME=libffmpeg.so
build
fi

#x86_64
if [[ " ${BUILD_ARCHS[*]} " == *" x86_64 "* ]] || [ "$BUILD_ALL" = true ]; then
ARCH=x86_64
EABIARCH=$ARCH-linux-android
OPTIMIZE_CFLAGS="-m64"
PREFIX=$(pwd)/../VPlayerLibrary2/src/main/cpp/ffmpeg-build/$ARCH
OUT_LIBRARY=$PREFIX/libffmpeg.so
ADDITIONAL_CONFIGURE_FLAG=--disable-asm
SONAME=libffmpeg.so
build
fi

#arm64-v8a
if [[ " ${BUILD_ARCHS[*]} " == *" arm64-v8a "* ]] || [ "$BUILD_ALL" = true ]; then
CPU=arm64
ARCH=$CPU
HOST=aarch64-linux
EABIARCH=$HOST-android
OPTIMIZE_CFLAGS=
PREFIX=$(pwd)/../VPlayerLibrary2/src/main/cpp/ffmpeg-build/arm64-v8a
OUT_LIBRARY=$PREFIX/libffmpeg.so
ADDITIONAL_CONFIGURE_FLAG=--enable-neon
SONAME=libffmpeg-neon.so
build
fi

#arm v7 + neon
if [[ " ${BUILD_ARCHS[*]} " == *" armeabi-v7a "* ]] || [ "$BUILD_ALL" = true ]; then
ARCH=arm
CPU=armv7-a
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=neon -marm -march=$CPU -mtune=cortex-a8 -mthumb -D__thumb__ "
PREFIX=$(pwd)/../VPlayerLibrary2/src/main/cpp/ffmpeg-build/armeabi-v7a
EABIARCH=arm-linux-androideabi
OUT_LIBRARY=$PREFIX/libffmpeg.so
ADDITIONAL_CONFIGURE_FLAG=--enable-neon
SONAME=libffmpeg.so
build
fi

echo "All built"
