#!/bin/bash
set -e
export APP_VERSION=$(date +"%y%m%d.%H%M%S")
export RTSPEXTRACT_NAME="rtspextract"
export EVA_IMAGE_FOLDER="edge-services"
export IMAGE_NAME="${RTSPEXTRACT_NAME}-image"
export IMAGE_TAG=${APP_VERSION}
export CONTAINER_NAME="${RTSPEXTRACT_NAME}-container"
export ALTERNATIVE_TAG="latest"

echo "APP_VERSION $APP_VERSION"
echo "IMAGE_NAME $IMAGE_NAME"
echo "IMAGE_TAG $IMAGE_TAG"
echo "ALTERNATIVE_TAG $ALTERNATIVE_TAG"

result=$(docker image inspect ${EVA_IMAGE_FOLDER}/$IMAGE_NAME:$ALTERNATIVE_TAG  2>/dev/null) || true
#f [[ ${result} == "[]" ]]; then
    cmd="docker build  -f Dockerfile -t ${EVA_IMAGE_FOLDER}/${IMAGE_NAME}:${IMAGE_TAG} . " 
    echo "$cmd"
    eval "$cmd"
    
    #docker push ${IMAGE_NAME}:${IMAGE_TAG}
    # Push with alternative tag
    cmd="docker tag ${EVA_IMAGE_FOLDER}/${IMAGE_NAME}:${IMAGE_TAG} ${EVA_IMAGE_FOLDER}/${IMAGE_NAME}:${ALTERNATIVE_TAG}"
    echo "$cmd"
    eval "$cmd"
    #docker push ${IMAGE_NAME}:${ALTERNATIVE_TAG}
#fi
docker stop ${CONTAINER_NAME} 2> /dev/null || true
cmd="docker run  -d -it --rm --log-driver json-file --log-opt max-size=1m --log-opt max-file=3  --name ${CONTAINER_NAME} ${EVA_IMAGE_FOLDER}/${IMAGE_NAME}:${ALTERNATIVE_TAG}" 
echo "$cmd"
eval "$cmd"

# Copy binaries to the host machine
cmd="docker create --name dummy ${EVA_IMAGE_FOLDER}/${IMAGE_NAME}:${ALTERNATIVE_TAG}"
echo "$cmd"
eval "$cmd"
mkdir ./ffmpegbinaries 2> /dev/null || true
DESTINATION_PATH="./ffmpegbinaries"
docker cp dummy:/usr/local/bin/ffmpeg ${DESTINATION_PATH}
docker cp dummy:/usr/local/bin/rtspextract ${DESTINATION_PATH}
docker cp dummy:/usr/local/lib/libavcodec.a  ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libavcodec.so ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libavcodec.so.59 ${DESTINATION_PATH}        
docker cp dummy:/usr/local/lib/libavcodec.so.59.37.100 ${DESTINATION_PATH} 
docker cp dummy:/usr/local/lib/libavdevice.so ${DESTINATION_PATH}          
docker cp dummy:/usr/local/lib/libavdevice.so.59 ${DESTINATION_PATH}       
docker cp dummy:/usr/local/lib/libavdevice.so.59.7.100 ${DESTINATION_PATH} 
docker cp dummy:/usr/local/lib/libavdevice.a ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libavfilter.so ${DESTINATION_PATH}          
docker cp dummy:/usr/local/lib/libavfilter.so.8 ${DESTINATION_PATH}        
docker cp dummy:/usr/local/lib/libavfilter.so.8.44.100 ${DESTINATION_PATH} 
docker cp dummy:/usr/local/lib/libavfilter.a ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libavformat.so.59.27.100 ${DESTINATION_PATH} 
docker cp dummy:/usr/local/lib/libavformat.a ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libavformat.so ${DESTINATION_PATH}          
docker cp dummy:/usr/local/lib/libavformat.so.59 ${DESTINATION_PATH}       
docker cp dummy:/usr/local/lib/libpostproc.a ${DESTINATION_PATH}           
docker cp dummy:/usr/local/lib/libpostproc.so.56 ${DESTINATION_PATH}       
docker cp dummy:/usr/local/lib/libpostproc.so ${DESTINATION_PATH}          
docker cp dummy:/usr/local/lib/libpostproc.so.56.6.100 ${DESTINATION_PATH} 
docker cp dummy:/usr/local/lib/libswresample.so ${DESTINATION_PATH}         
docker cp dummy:/usr/local/lib/libswresample.so.4.7.100 ${DESTINATION_PATH}
docker cp dummy:/usr/local/lib/libswresample.so.4 ${DESTINATION_PATH}       
docker cp dummy:/usr/local/lib/libswresample.a ${DESTINATION_PATH}         
docker cp dummy:/usr/local/lib/libswscale.so.6 ${DESTINATION_PATH}
docker cp dummy:/usr/local/lib/libswscale.so.6.7.100 ${DESTINATION_PATH}
docker cp dummy:/usr/local/lib/libswscale.a  ${DESTINATION_PATH}            
docker cp dummy:/usr/local/lib/libswscale.so ${DESTINATION_PATH}            
docker cp dummy:/usr/local/lib/libavutil.a ${DESTINATION_PATH}              
docker cp dummy:/usr/local/lib/libavutil.so ${DESTINATION_PATH}             
docker cp dummy:/usr/local/lib/libavutil.so.57 ${DESTINATION_PATH}          
docker cp dummy:/usr/local/lib/libavutil.so.57.28.100 ${DESTINATION_PATH}  
docker cp dummy:/usr/lib/x86_64-linux-gnu/libva.so.2 ${DESTINATION_PATH}   
docker cp dummy:/usr/lib/x86_64-linux-gnu/libva.so.2.700.0   ${DESTINATION_PATH}
docker cp dummy:/usr/lib/x86_64-linux-gnu/libva-drm.so.2 ${DESTINATION_PATH}   
docker cp dummy:/usr/lib/x86_64-linux-gnu/libva-drm.so.2.700.0  ${DESTINATION_PATH}
docker cp dummy:/usr/lib/x86_64-linux-gnu/libdrm.so.2  ${DESTINATION_PATH}
docker cp dummy:/usr/lib/x86_64-linux-gnu/libdrm.so.2.4.0   ${DESTINATION_PATH}
docker cp dummy:/usr/lib/x86_64-linux-gnu/libssl.so.1.1   ${DESTINATION_PATH}
docker cp dummy:/usr/lib/x86_64-linux-gnu/libcrypto.so.1.1    ${DESTINATION_PATH}
docker rm -f dummy

    
echo "Extended version of ffmpeg to extract source RTSP clock available" 
echo "Syntax to extract frame:"
echo "  ffmpeg -rtsp_transport tcp   -r [SOURCE_FRAME_RATE] -i rtsp://[RTSP_SERVER_HOST]:[RTSP_SERVER_PORT]/[RTSP_SOURCE_PATH] -vf fps=[NUMBER_OF_FRAME_PER_SECOND]  -strftime 1  [LOCAL_PATH]_%018.6f.jpg"
echo "  For instance: /opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i RTSP_URL -vf fps=1  -strftime 1  /tmp/frame_%018.6f.jpg"
echo "Syntax to extract frame:"
echo "  ffmpeg -rtsp_transport tcp   -r [SOURCE_FRAME_RATE] -i rtsp://[RTSP_SERVER_HOST]:[RTSP_SERVER_PORT]/[RTSP_SOURCE_PATH]  -acodec copy -f segment -segment_time [CHUNK_DURATION_SECOND]  -vcodec copy  -copyts -movflags faststart  -strftime 1  [LOCAL_PATH]_%018.6f.mp4"
echo "  For instance: /opt/ffmpeg/bin/ffmpeg -rtsp_transport tcp   -r 25 -i RTSP_URL -acodec copy -f segment -segment_time 10  -vcodec copy  -copyts -movflags faststart -strftime 1  /tmp/chunk_%018.6f.mp4"

echo "Run ffmpeg and rtspextract locally with the following command lines:"
echo "  LD_LIBRARY_PATH=./ffmpegbinaries && ./ffmpegbinaries/ffmpeg"
echo "  LD_LIBRARY_PATH=./ffmpegbinaries && ./ffmpegbinaries/rtspextract"
echo "Run ffmpeg and rtspextract locally in a container with the following command lines:"
echo "  docker run -it --rm --log-driver json-file --log-opt max-size=1m --log-opt max-file=3  --name dummy edge-services/rtspextract-image:latest /usr/local/bin/ffmpeg"
echo "  docker run -it --rm --log-driver json-file --log-opt max-size=1m --log-opt max-file=3  --name dummy edge-services/rtspextract-image:latest /usr/local/bin/rtspextract"

