#define MINIMP4_IMPLEMENTATION
#ifdef _WIN32
#include <sys/types.h>
#include <stddef.h>
typedef size_t ssize_t;
#endif
#include "minimp4.h"
#define ENABLE_AUDIO 0
#if ENABLE_AUDIO
#include <fdk-aac/aacenc_lib.h>
#include <fdk-aac/aacdecoder_lib.h>
#define AUDIO_RATE 12000
#endif

#define VIDEO_FPS 30

static uint8_t *preload(const char *path, ssize_t *data_size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    *data_size = 0;
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END))
        exit(1);
    *data_size = (ssize_t)ftell(file);
    if (*data_size < 0)
        exit(1);
    if (fseek(file, 0, SEEK_SET))
        exit(1);
    data = (unsigned char*)malloc(*data_size);
    if (!data)
        exit(1);
    if ((ssize_t)fread(data, 1, *data_size, file) != *data_size)
        exit(1);
    fclose(file);
    return data;
}

static ssize_t get_nal_size(uint8_t *buf, ssize_t size)
{
    ssize_t pos = 3;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
            return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
            return pos;
        pos++;
    }
    return size;
}

static int write_callback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: minimp4 in.h264 out.mp4\n");
        return 0;
    }
    ssize_t h264_size;
    uint8_t *alloc_buf;
    uint8_t *buf_h264 = alloc_buf = preload(argv[1], &h264_size);
    if (!buf_h264)
    {
        printf("error: can't open h264 file\n");
        return 0;
    }

    FILE *fout = fopen(argv[2], "wb");
    if (!fout)
    {
        printf("error: can't open output file\n");
        return 0;
    }
    int is_hevc = (0 != strstr(argv[1], "265")) || (0 != strstr(argv[1], "hevc"));

    int sequential_mode = 0;
    int enable_fragmentation = 0;
    MP4E_mux_t *mux;
    mp4_h26x_writer_t mp4wr;
    mux = MP4E_open(sequential_mode, enable_fragmentation, fout, write_callback);
    mp4_h26x_write_init(&mp4wr, mux, 352, 288, is_hevc);

#if ENABLE_AUDIO
    ssize_t pcm_size;
    int16_t *alloc_pcm;
    int16_t *buf_pcm  = alloc_pcm = (int16_t *)preload("stream.pcm", &pcm_size);
    if (!buf_pcm)
    {
        printf("error: can't open pcm file\n");
        return 0;
    }
    uint32_t sample = 0, total_samples = pcm_size/2;
    uint64_t ts = 0, ats = 0;
    HANDLE_AACENCODER aacenc;
    AACENC_InfoStruct info;
    aacEncOpen(&aacenc, 0, 0);
    aacEncoder_SetParam(aacenc, AACENC_TRANSMUX, 0);
    aacEncoder_SetParam(aacenc, AACENC_AFTERBURNER, 1);
    aacEncoder_SetParam(aacenc, AACENC_BITRATE, 64000);
    aacEncoder_SetParam(aacenc, AACENC_SAMPLERATE, AUDIO_RATE);
    aacEncoder_SetParam(aacenc, AACENC_CHANNELMODE, 1);
    aacEncEncode(aacenc, NULL, NULL, NULL, NULL);
    aacEncInfo(aacenc, &info);

    MP4E_track_t tr;
    tr.track_media_kind = e_audio;
    tr.language[0] = 'u';
    tr.language[1] = 'n';
    tr.language[2] = 'd';
    tr.language[3] = 0;
    tr.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
    tr.time_scale = 90000;
    tr.default_duration = 0;
    tr.u.a.channelcount = 1;
    int audio_track_id = MP4E_add_track(mux, &tr);
    MP4E_set_dsi(mux, audio_track_id, info.confBuf, info.confSize);
#endif
    while (h264_size > 0)
    {
        ssize_t nal_size = get_nal_size(buf_h264, h264_size);
        if (nal_size < 4)
        {
            buf_h264  += 1;
            h264_size -= 1;
            continue;
        }
        /*int startcode_size = 4;
        if (buf_h264[0] == 0 && buf_h264[1] == 0 && buf_h264[2] == 1)
            startcode_size = 3;
        int nal_type = buf_h264[startcode_size] & 31;
        int is_intra = (nal_type == 5);
        printf("nal size=%ld, nal_type=%d\n", nal_size, nal_type);*/

        int ret = mp4_h26x_write_nal(&mp4wr, buf_h264, nal_size, 90000/VIDEO_FPS);
        buf_h264  += nal_size;
        h264_size -= nal_size;

#if ENABLE_AUDIO
        ts += 90000/VIDEO_FPS;
        while (ats < ts)
        {
            AACENC_BufDesc in_buf, out_buf;
            AACENC_InArgs  in_args;
            AACENC_OutArgs out_args;
            uint8_t buf[2048];
            if (total_samples < 1024)
            {
                buf_pcm = alloc_pcm;
                total_samples = pcm_size/2;
            }
            in_args.numInSamples = 1024;
            void *in_ptr = buf_pcm, *out_ptr = buf;
            int in_size          = 2*in_args.numInSamples;
            int in_element_size  = 2;
            int in_identifier    = IN_AUDIO_DATA;
            int out_size         = sizeof(buf);
            int out_identifier   = OUT_BITSTREAM_DATA;
            int out_element_size = 1;

            in_buf.numBufs            = 1;
            in_buf.bufs               = &in_ptr;
            in_buf.bufferIdentifiers  = &in_identifier;
            in_buf.bufSizes           = &in_size;
            in_buf.bufElSizes         = &in_element_size;
            out_buf.numBufs           = 1;
            out_buf.bufs              = &out_ptr;
            out_buf.bufferIdentifiers = &out_identifier;
            out_buf.bufSizes          = &out_size;
            out_buf.bufElSizes        = &out_element_size;

            if (AACENC_OK != aacEncEncode(aacenc, &in_buf, &out_buf, &in_args, &out_args))
            {
                printf("error: aac encode fail\n");
                exit(1);
            }
            sample  += in_args.numInSamples;
            buf_pcm += in_args.numInSamples;
            total_samples -= in_args.numInSamples;
            ats = (uint64_t)sample*90000/AUDIO_RATE;

            MP4E_put_sample(mux, audio_track_id, buf, out_args.numOutBytes, 1024*90000/AUDIO_RATE, MP4E_SAMPLE_RANDOM_ACCESS);
        }
#endif
    }
    if (alloc_buf)
        free(alloc_buf);
    MP4E_close(mux);
    if (fout)
        fclose(fout);
}
