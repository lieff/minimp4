_FILENAME=${0##*/}
CUR_DIR=${0/${_FILENAME}}
CUR_DIR=$(cd $(dirname ${CUR_DIR}); pwd)/$(basename ${CUR_DIR})/

pushd $CUR_DIR/..

arm-linux-gnueabihf-gcc -static -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -DNDEBUG -D_FILE_OFFSET_BITS=64 \
-flto -O3 -std=gnu11 -ffast-math -fomit-frame-pointer -ftree-vectorize \
-o minimp4_arm_gcc minimp4_test.c -lm -lpthread