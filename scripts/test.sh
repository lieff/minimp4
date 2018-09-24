_FILENAME=${0##*/}
CUR_DIR=${0/${_FILENAME}}
CUR_DIR=$(cd $(dirname ${CUR_DIR}); pwd)/$(basename ${CUR_DIR})/

pushd $CUR_DIR/..

./minimp4_x86 vectors/foreman.264 out.mp4
if ! cmp ./out.mp4 vectors/out_ref.mp4 >/dev/null 2>&1
then
    echo test failed
    exit 1
fi
rm out.mp4
qemu-arm ./minimp4_arm_gcc vectors/foreman.264 out.mp4
if ! cmp ./out.mp4 vectors/out_ref.mp4 >/dev/null 2>&1
then
    echo test failed
    exit 1
fi
rm out.mp4
echo test passed
