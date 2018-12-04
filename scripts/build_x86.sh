_FILENAME=${0##*/}
CUR_DIR=${0/${_FILENAME}}
CUR_DIR=$(cd $(dirname ${CUR_DIR}); pwd)/$(basename ${CUR_DIR})/

pushd $CUR_DIR/..

gcc -flto -O3 -std=gnu11 -DNDEBUG -D_FILE_OFFSET_BITS=64 \
-fno-stack-protector -ffunction-sections -fdata-sections -Wl,--gc-sections \
-o minimp4_x86 minimp4_test.c -lm -lpthread