#!/bin/bash
set -e
BASH_SCRIPT=`readlink -f "$0"`
BASH_DIR=`dirname "$BASH_SCRIPT"`
cd "$BASH_DIR"
##############################################################################
# colors for formatting the ouput
##############################################################################
# shellcheck disable=SC2034
{
YELLOW='\033[1;33m'
GREEN='\033[1;32m'
RED='\033[0;31m'
BLUE='\033[1;34m'
NC='\033[0m' # No Color
}
##############################################################################
#- function used to check whether an error occured
##############################################################################
checkError() {
    # shellcheck disable=SC2181
    if [ $? -ne 0 ]; then
        echo -e "${RED}\nAn error occured exiting from the current bash${NC}"
        exit 1
    fi
}
##############################################################################
#- print functions
##############################################################################
printMessage(){
    echo -e "${GREEN}$1${NC}" 
}
printWarning(){
    echo -e "${YELLOW}$1${NC}" 
}
printError(){
    echo -e "${RED}$1${NC}" 
}
printProgress(){
    echo -e "${BLUE}$1${NC}" 
}
#######################################################
#- function used to print out script usage
#######################################################
usage() {
    echo
    echo "Build ffmpeg and tools :"
    echo "Arguments:"
    echo -e " -d  Sets debug option: 1 debug 0 no debug (default)"
    echo -e " -i  Sets interactive option: 1 interactive 0 no interaction - silent mode (default)) "
    echo
    echo "Example:"
    echo -e " bash ./build.sh -d 1 -i 1 "    
}

pushd "${BASH_DIR}"
printMessage "Installing pre-requisites"

debug=0
interactive=0
while getopts "d:i:" opt; do
    case $opt in
    d) debug=$OPTARG ;;
    i) interactive=$OPTARG ;;    
    :)
        echo "Error: -${OPTARG} requires a value"
        usage
        exit 1
        ;;
    *)
        usage
        exit 1
        ;;
    esac
done

if [ $debug != 0 ];
then
   #export DEBUG_DISABLED="--disable-optimizations --extra-cflags=-Og --extra-cflags=-fno-omit-frame-pointer --enable-debug=3 --extra-cflags=-fno-inline"
   export DEBUG_DISABLED=" --enable-shared --disable-static --disable-optimizations --disable-mmx --disable-stripping --enable-debug=3 "
   export CFLAGS=" -g" 
else
   export DEBUG_DISABLED="--disable-debug"
fi
sudo apt-get -yqq update && \
        sudo apt-get install -yq --no-install-recommends ca-certificates expat libgomp1 && \
        sudo apt-get autoremove -y && \
        sudo apt-get clean -y

export FFMPEG_VERSION=5.1.2 
export SRC=/usr/local
export LD_LIBRARY_PATH=/opt/ffmpeg/lib
export MAKEFLAGS="-j2"
export PKG_CONFIG_PATH="/opt/ffmpeg/share/pkgconfig:/opt/ffmpeg/lib/pkgconfig:/opt/ffmpeg/lib64/pkgconfig"
export PREFIX=/opt/ffmpeg
export LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/ffmpeg/lib64"
export DEBIAN_FRONTEND=noninteractive
export INSTALL_DIR=/tmp/ffmpeg

export buildDeps="autoconf \
                    automake \
                    cmake \
                    curl \
                    bzip2 \
                    libexpat1-dev \
                    g++ \
                    gcc \
                    gdb \
                    git \
                    gperf \
                    libtool \
                    make \
                    meson \
                    nasm \
                    perl \
                    pkg-config \
                    python \
                    libssl-dev \
                    yasm \
                    libva-dev \
                    zlib1g-dev"
            
sudo apt-get -yqq update 
sudo apt-get install -yq --no-install-recommends ${buildDeps}
if [ $interactive != 0 ];
then
        read -p "Press any key to resume ..."
fi


popd
printMessage "Installing building environment"
pushd "${BASH_DIR}"
sudo rm -rf ${INSTALL_DIR}/* || true
sudo mkdir  ${INSTALL_DIR} || true
cd ${INSTALL_DIR}
export DIR=${INSTALL_DIR} 
printMessage "Download ffmpeg version ${FFMPEG_VERSION} souce code"
sudo curl -sLO https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2
printMessage "Unzip ffmpeg version ${FFMPEG_VERSION} souce code"
sudo tar -jx --strip-components=1 -f ffmpeg-${FFMPEG_VERSION}.tar.bz2 
popd

printMessage "Copying custom code"
sudo cp ./rtsp.c ${INSTALL_DIR}/libavformat
sudo cp ./rtsp.h ${INSTALL_DIR}/libavformat
sudo cp ./segment.c ${INSTALL_DIR}/libavformat
sudo cp ./img2enc.c ${INSTALL_DIR}/libavformat
sudo cp ./avformat.h ${INSTALL_DIR}/libavformat
sudo cp ./ffmpeg.c ${INSTALL_DIR}/fftools/ffmpeg.c
sudo cp ./rtspextract.c ${INSTALL_DIR}


pushd "${BASH_DIR}"
cd ${INSTALL_DIR}
printMessage "Configure ffmpeg env"
printMessage "sudo ./configure      ${DEBUG_DISABLED} --disable-doc    --disable-ffplay   --enable-shared --enable-gpl  --extra-libs=-ldl"
sudo ./configure      ${DEBUG_DISABLED} --disable-doc    --disable-ffplay   --enable-shared --enable-gpl  --extra-libs=-ldl
printMessage "Make"
sudo make ;  sudo make install
if [ $interactive != 0 ];
then
        read -p "Press any key to resume ..."
fi

popd 



printMessage "Building ffmpeg and libraries"
pushd "${BASH_DIR}"
## Build ffmpeg https://ffmpeg.org/
cd ${INSTALL_DIR}
export DIR=${INSTALL_DIR} 
printMessage "Configure rtspextract env"
printMessage "sudo ./configure \
        ${DEBUG_DISABLED} \
        --disable-doc \
        --disable-ffplay \
        --enable-gpl \
        --enable-nonfree \
        --enable-openssl \
        --enable-postproc \
        --enable-shared \
        --enable-small \
       --enable-vaapi \
        --enable-version3 \
        --extra-cflags=\"-I${PREFIX}/include\" \
        --extra-ldflags=\"-L${PREFIX}/lib\" \
        --extra-libs=-ldl \
        --extra-libs=-lpthread \
        --prefix=\"${PREFIX}\" "

sudo ./configure \
        ${DEBUG_DISABLED} \
        --disable-doc \
        --disable-ffplay \
        --enable-gpl \
        --enable-nonfree \
        --enable-openssl \
        --enable-postproc \
        --enable-shared \
        --enable-small \
       --enable-vaapi \
        --enable-version3 \
        --extra-cflags="-I${PREFIX}/include" \
        --extra-ldflags="-L${PREFIX}/lib" \
        --extra-libs=-ldl \
        --extra-libs=-lpthread \
        --prefix="${PREFIX}" 

printMessage "Make clean"
############        sudo make clean
printMessage "Make"
        sudo make 
printMessage "Make install"
        sudo make install 
#        sudo make tools/zmqsend && sudo cp tools/zmqsend ${PREFIX}/bin/ && \
#        sudo make tools/extract && sudo cp tools/extract ${PREFIX}/bin/ && \
printMessage "Make distclean"
############        sudo make distclean 
        hash -r
printMessage "Make qt-faststart"
        cd tools && \
        sudo make qt-faststart && sudo cp qt-faststart ${PREFIX}/bin/

printMessage "Copy ffmpeg libraries"
sudo ldd ${PREFIX}/bin/ffmpeg | grep opt/ffmpeg | cut -d ' ' -f 3 | xargs -i sudo cp {} /usr/local/lib/

printMessage "Install ffmpeg libraries and dpackages config"
for lib in /usr/local/lib/*.so.*; do sudo ln -sf "${lib##*/}" "${lib%%.so.*}".so; done
sudo cp ${PREFIX}/bin/* /usr/local/bin/ && \
sudo cp -r ${PREFIX}/share/ffmpeg /usr/local/share/ && \
LD_LIBRARY_PATH=/usr/local/lib ffmpeg -buildconf && \
sudo cp -r ${PREFIX}/include/libav* ${PREFIX}/include/libpostproc ${PREFIX}/include/libsw* /usr/local/include && \
sudo mkdir -p /usr/local/lib/pkgconfig && \
for pc in ${PREFIX}/lib/pkgconfig/libav*.pc ${PREFIX}/lib/pkgconfig/libpostproc.pc ${PREFIX}/lib/pkgconfig/libsw*.pc; do \
        sudo chmod 0766 /usr/local/lib/pkgconfig/"${pc##*/}"; \
        sudo sed "s:${PREFIX}:/usr/local:g" <"$pc" >/usr/local/lib/pkgconfig/"${pc##*/}"; \
done
if [ $interactive != 0 ];
then
        read -p "Press any key to resume ..."
fi

popd 

printMessage "Building rtspextract"
pushd "${BASH_DIR}"


cd ${INSTALL_DIR}
export DIR=${INSTALL_DIR} 
export PREFIX=/opt/ffmpeg
export SOURCE_DIR=${INSTALL_DIR}
export BUILD_DIR=/opt/ffmpeg
export PKG_CONFIG_PATH="${PREFIX}"
printMessage "sudo ./configure --prefix=\"${PREFIX}\" ${DEBUG_DISABLED} --pkg-config-flags=\"--static\" --extra-cflags=\"-I${PREFIX}/include\" --extra-ldflags=\"-L${PREFIX}/lib\" --bindir=\"${PREFIX}/bin\" --enable-gpl --enable-nonfree"
sudo ./configure --prefix="${PREFIX}" ${DEBUG_DISABLED} --pkg-config-flags="--static" --extra-cflags="-I${PREFIX}/include" --extra-ldflags="-L${PREFIX}/lib" --bindir="${PREFIX}/bin" --enable-gpl --enable-nonfree
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig/" 
sudo gcc -I${SOURCE_DIR}/ rtspextract.c -o rtspextract -lm `pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample`   -lva-drm 
sudo cp ${DIR}/rtspextract /usr/local/bin

#rtspextract -f -i RTSP_URL -e 25 -o /tmp/frame-%s.jpg
#/opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i rtsp://172.17.0.3:554/media/camera-300s.mp4  -vf fps=5 -strftime 1  "/tmp/frame_%018.6f.jpg"
#/opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i rtsp://172.17.0.3:554/media/camera-300s.mp4  -acodec copy -f segment -segment_time 10  -vcodec copy  -copyts -strftime 1  "/tmp/chunk_%018.6f.mp4"
#/opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i RTSP_URL -vf fps=1  -strftime 1  /tmp/frame_%018.6f.jpg
#/opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i RTSP_URL -acodec copy -f segment -segment_time 10  -vcodec copy  -copyts -strftime 1  /tmp/chunk_%018.6f.mp4

export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64
popd
printMessage "Building done..."
printMessage "Run this command before running ffmpeg or rtspextract 'export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64'"
printMessage "Extract chunk command: ffmpeg -rtsp_transport tcp   -r [frame per second for the source] -i [rtsp source]  -acodec copy -f segment -segment_time [chunk duration in second]  -vcodec copy  -copyts -strftime 1  /tmp/chunk_%018.6f.mp4"
printMessage "Extract frame command: ffmpeg -rtsp_transport tcp   -r [frame per second for the source] -i [rtsp source]  -vf fps=[number of frames per second] -strftime 1  /tmp/frame_%018.6f.jpg"

if [ $debug != 0 ];
then
    printMessage "Debug you can run the following command: 'gdb --args ${INSTALL_DIR}/ffmpeg_g --help'"
fi