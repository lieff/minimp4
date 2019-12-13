_FILENAME=${0##*/}
CUR_DIR=${0/${_FILENAME}}
CUR_DIR=$(cd $(dirname ${CUR_DIR}); pwd)/$(basename ${CUR_DIR})/

pushd $CUR_DIR/..

test_executable() {

EXEC=$1

$EXEC vectors/foreman.264 out.mp4
if ! cmp ./out.mp4 vectors/out_ref.mp4 >/dev/null 2>&1
then
    echo test failed
    exit 1
fi
rm out.mp4

$EXEC -s vectors/foreman.264 out.mp4
if ! cmp ./out.mp4 vectors/out_sequential_ref.mp4 >/dev/null 2>&1
then
    echo sequential test failed
    exit 1
fi
rm out.mp4

$EXEC -f vectors/foreman.264 out.mp4
if ! cmp ./out.mp4 vectors/out_fragmentation_ref.mp4 >/dev/null 2>&1
then
    echo fragmentation test failed
    exit 1
fi
rm out.mp4

$EXEC vectors/foreman_slices.264 out.mp4
if ! cmp ./out.mp4 vectors/out_slices_ref.mp4 >/dev/null 2>&1
then
    echo slices test failed
    exit 1
fi
rm out.mp4

$EXEC -d vectors/out_ref.mp4 out.h264
if ! cmp ./out.h264 vectors/foreman.264 >/dev/null 2>&1
then
    echo demux test failed
    exit 1
fi
rm out.h264

$EXEC -d vectors/out_sequential_ref.mp4 out.h264
if ! cmp ./out.h264 vectors/foreman.264 >/dev/null 2>&1
then
    echo demux sequential test failed
    exit 1
fi
rm out.h264

$EXEC -d vectors/out_slices_ref.mp4 out.h264
if ! cmp ./out.h264 vectors/foreman_slices.264 >/dev/null 2>&1
then
    echo demux slices test failed
    exit 1
fi
rm out.h264

}

test_executable ./minimp4_x86
test_executable "qemu-arm ./minimp4_arm_gcc"

echo test passed
