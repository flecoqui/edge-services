# ffmpeg - http://ffmpeg.org/download.html
#
# From https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu
#
# https://hub.docker.com/r/jrottenberg/ffmpeg/
#
#
FROM        ubuntu:20.04 AS base

WORKDIR     /tmp/workdir

RUN     apt-get -y update && \
        apt-get install -y --no-install-recommends ca-certificates expat libgomp1 && \
        apt-get autoremove -y && \
        apt-get clean -y

FROM base as build

ENV         FFMPEG_VERSION=5.1.2 \
    SRC=/usr/local


ARG         LD_LIBRARY_PATH=/opt/ffmpeg/lib
ARG         MAKEFLAGS="-j2"
ARG         PKG_CONFIG_PATH="/opt/ffmpeg/share/pkgconfig:/opt/ffmpeg/lib/pkgconfig:/opt/ffmpeg/lib64/pkgconfig"
ARG         PREFIX=/opt/ffmpeg
ARG         LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/ffmpeg/lib64"


ARG DEBIAN_FRONTEND=noninteractive

RUN      buildDeps="autoconf \
                    automake \
                    cmake \
                    curl \
                    bzip2 \
                    libexpat1-dev \
                    g++ \
                    gcc \
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
                    zlib1g-dev" && \
        apt-get -y update && \
        apt-get install -y --no-install-recommends ${buildDeps}

## Download ffmpeg https://ffmpeg.org/
RUN  \
        DIR=/tmp/ffmpeg && mkdir -p ${DIR} && cd ${DIR} && \
        curl -sLO https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2 && \
        tar -jx --strip-components=1 -f ffmpeg-${FFMPEG_VERSION}.tar.bz2 

COPY rtsp.c /tmp/ffmpeg/libavformat
COPY rtsp.h /tmp/ffmpeg/libavformat
COPY segment.c /tmp/ffmpeg/libavformat
COPY img2enc.c /tmp/ffmpeg/libavformat
COPY avformat.h /tmp/ffmpeg/libavformat
COPY ffmpeg.c /tmp/ffmpeg/fftools/ffmpeg.c

RUN     DIR=/tmp/ffmpeg && cd ${DIR} && \
        ./configure     --disable-debug  --disable-doc    --disable-ffplay   --enable-shared --enable-gpl  --extra-libs=-ldl && \
        make ;  make install



## Build ffmpeg https://ffmpeg.org/
RUN  \
        DIR=/tmp/ffmpeg && cd ${DIR} && \
        ./configure \
        --disable-debug \
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
        --prefix="${PREFIX}" && \
        make clean && \
        make && \
        make install && \
        make distclean && \
        hash -r && \
        cd tools && \
        make qt-faststart && cp qt-faststart ${PREFIX}/bin/



## cleanup
RUN \
        ldd ${PREFIX}/bin/ffmpeg | grep opt/ffmpeg | cut -d ' ' -f 3 | xargs -i cp {} /usr/local/lib/ && \
        for lib in /usr/local/lib/*.so.*; do rm "${lib%%.so.*}".so || true;  ln -s "${lib##*/}" "${lib%%.so.*}".so; done && \
        cp ${PREFIX}/bin/* /usr/local/bin/ && \
        cp -r ${PREFIX}/share/ffmpeg /usr/local/share/ && \
        LD_LIBRARY_PATH=/usr/local/lib ffmpeg -buildconf && \
        cp -r ${PREFIX}/include/libav* ${PREFIX}/include/libpostproc ${PREFIX}/include/libsw* /usr/local/include && \
        mkdir -p /usr/local/lib/pkgconfig && \
        for pc in ${PREFIX}/lib/pkgconfig/libav*.pc ${PREFIX}/lib/pkgconfig/libpostproc.pc ${PREFIX}/lib/pkgconfig/libsw*.pc; do \
          sed "s:${PREFIX}:/usr/local:g" <"$pc" >/usr/local/lib/pkgconfig/"${pc##*/}"; \
        done

## flecoqui
COPY rtspextract.c /tmp/ffmpeg
COPY rtsp.c /tmp/ffmpeg/libavformat
COPY rtsp.h /tmp/ffmpeg/libavformat
COPY segment.c /tmp/ffmpeg/libavformat
COPY img2enc.c /tmp/ffmpeg/libavformat
COPY avformat.h /tmp/ffmpeg/libavformat
COPY ffmpeg.c /tmp/ffmpeg/fftools/ffmpeg.c

RUN \
        DIR=/tmp/ffmpeg && cd ${DIR} && \
        SOURCE_DIR=/tmp/ffmpeg && \
        BUILD_DIR=/opt/ffmpeg && \
        PKG_CONFIG_PATH="${PREFIX}" && ./configure --prefix="${PREFIX}" --pkg-config-flags="--static" --extra-cflags="-I${PREFIX}/include" --extra-ldflags="-L${PREFIX}/lib" --bindir="${PREFIX}/bin" --enable-gpl --enable-nonfree && \
        PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig/" && gcc -I${SOURCE_DIR}/ rtspextract.c -o rtspextract -lm `pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample`   -lva-drm && \
        cp ${DIR}/rtspextract /usr/local/bin


FROM        base AS release

ENV         LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64

CMD         ["/bin/bash"]
# CMD         ["--help"]
# ENTRYPOINT  ["ffmpeg"]

COPY --from=build /usr/local /usr/local/

RUN \
	apt-get update -y && \
	apt-get install -y --no-install-recommends libva-drm2 libva2  && \
	rm -rf /var/lib/apt/lists/*
