#!/bin/bash
DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
GIT_SSL_NO_VERIFY=true

function addAutomakeOpts() {
    if !(grep -Rq "AUTOMAKE_OPTIONS" Makefile.am)
    then
        sed -i '1iAUTOMAKE_OPTIONS=subdir-objects' Makefile.am
    fi
}

cd ..
git submodule update --init --recursive
cd ffmpeg_build

# Configure libjpeg
cd libjpeg-turbo
autoreconf -fiv
cd ..

# configure the environment
cd libpng
sh ./autogen.sh
cd ..

# configure the environment
cd freetype2
sh ./autogen.sh
cd ..

cd util-linux
sh ./autogen.sh
cd ..

cd libxml2
sh ./autogen.sh
cd ..

cd fontconfig
sh ./autogen.sh
cd ..

# GMP needed by gnutls and nettle
if [ ! -d gmp ]; then
    VER=6.1.2
    FILE=gmp-$VER.tar.xz
    wget https://gmplib.org/download/gmp/$FILE
    echo "Extracting gmp $VER..."
    tar -xf $FILE
    mv gmp-$VER/ gmp/
    rm $FILE
fi

# gnutls
if [ ! -d gnutls ]; then
    VER=3.6.2
    SUBVER=`echo $VER | sed -r 's/\.[0-9]+$//'`
    FILE=gnutls-$VER.tar.xz
    wget https://www.gnupg.org/ftp/gcrypt/gnutls/v3.6/$FILE
    echo "Extracting gnutls $VER..."
    tar -xf $FILE
    mv gnutls-$VER/ gnutls/
    rm $FILE
fi

# configure the environment
cd gnutls
make bootstrap
autoreconf -ivf
cd ..

# configure the environment
cd nettle
./.bootstrap
cd ..

# fribidi
cd fribidi
./bootstrap
cd ..

# libass
cd libass
autoreconf -ivf
cd ..

# aacenc environment
cd vo-aacenc
addAutomakeOpts
autoreconf -ivf
cd ..

# opencore-amr-code environment
cd opencore-amr-code
addAutomakeOpts
autoreconf -ivf
cd ..

# fdk-aac environment
cd fdk-aac
sh ./autogen.sh
cd ..

# Start the build!
source build_android.sh "$@"

