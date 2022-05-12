# Build Stage
FROM --platform=linux/amd64 ubuntu:20.04 as builder

## Install build dependencies.
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y gcc

## Add source code to the build stage.
ADD . /minimp4
WORKDIR /minimp4

## TODO: ADD YOUR BUILD INSTRUCTIONS HERE.
RUN gcc -flto -O3 -std=gnu11 -DNDEBUG -D_FILE_OFFSET_BITS=64 \
-fno-stack-protector -ffunction-sections -fdata-sections -Wl,--gc-sections \
-o minimp4_x86 minimp4_test.c -lm -lpthread

# Package Stage
FROM --platform=linux/amd64 ubuntu:20.04

## TODO: Change <Path in Builder Stage>
COPY --from=builder /minimp4/minimp4_x86 /

