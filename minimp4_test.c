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

typedef struct
{
    uint8_t *buffer;
    ssize_t size;
} INPUT_BUFFER;

static int read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
    INPUT_BUFFER *buf = (INPUT_BUFFER*)token;
    size_t to_copy = MINIMP4_MIN(size, buf->size - offset - size);
    memcpy(buffer, buf->buffer + offset, to_copy);
    return to_copy != size;
}

int demux(uint8_t *input_buf, ssize_t input_size, FILE *fout, int ntrack)
{
    int /*ntrack, */i, spspps_bytes;
    const void *spspps;
    INPUT_BUFFER buf = { input_buf, input_size };
    MP4D_demux_t mp4 = { 0, };
    MP4D_open(&mp4, read_callback, &buf, input_size);

    //for (ntrack = 0; ntrack < mp4.track_count; ntrack++)
    {
        MP4D_track_t *tr = mp4.track + ntrack;
        unsigned sum_duration = 0;
        i = 0;
        if (tr->handler_type == MP4D_HANDLER_TYPE_VIDE)
        {   // assume h264
#define USE_SHORT_SYNC 0
            char sync[4] = { 0, 0, 0, 1 };
            while (spspps = MP4D_read_sps(&mp4, ntrack, i, &spspps_bytes))
            {
                fwrite(sync + USE_SHORT_SYNC, 1, 4 - USE_SHORT_SYNC, fout);
                fwrite(spspps, 1, spspps_bytes, fout);
                i++;
            }
            i = 0;
            while (spspps = MP4D_read_pps(&mp4, ntrack, i, &spspps_bytes))
            {
                fwrite(sync + USE_SHORT_SYNC, 1, 4 - USE_SHORT_SYNC, fout);
                fwrite(spspps, 1, spspps_bytes, fout);
                i++;
            }
            for (i = 0; i < mp4.track[ntrack].sample_count; i++)
            {
                unsigned frame_bytes, timestamp, duration;
                MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
                uint8_t *mem = input_buf + ofs;
                sum_duration += duration;
                while (frame_bytes)
                {
                    uint32_t size = ((uint32_t)mem[0] << 24) | ((uint32_t)mem[1] << 16) | ((uint32_t)mem[2] << 8) | mem[3];
                    size += 4;
                    mem[0] = 0; mem[1] = 0; mem[2] = 0; mem[3] = 1;
                    fwrite(mem + USE_SHORT_SYNC, 1, size - USE_SHORT_SYNC, fout);
                    if (frame_bytes < size)
                    {
                        printf("error: demux sample failed\n");
                        exit(1);
                    }
                    frame_bytes -= size;
                    mem += size;
                }
            }
        } else if (tr->handler_type == MP4D_HANDLER_TYPE_SOUN)
        {   // assume aac
#if ENABLE_AUDIO
            HANDLE_AACDECODER dec = aacDecoder_Open(TT_MP4_RAW, 1);
            UCHAR *dsi = (UCHAR *)tr->dsi;
            UINT dsi_size = tr->dsi_bytes;
            if (AAC_DEC_OK != aacDecoder_ConfigRaw(dec, &dsi, &dsi_size))
            {
                printf("error: aac config fail\n");
                exit(1);
            }
#endif
            for (i = 0; i < mp4.track[ntrack].sample_count; i++)
            {
                unsigned frame_bytes, timestamp, duration;
                MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
                printf("ofs=%d frame_bytes=%d timestamp=%d duration=%d\n", (unsigned)ofs, frame_bytes, timestamp, duration);
#if ENABLE_AUDIO
                UCHAR *frame = (UCHAR *)(input_buf + ofs);
                UINT frame_size = frame_bytes;
                UINT valid = frame_size;
                if (AAC_DEC_OK != aacDecoder_Fill(dec, &frame, &frame_size, &valid))
                {
                    printf("error: aac decode fail\n");
                    exit(1);
                }
                INT_PCM pcm[2048*8];
                int err = aacDecoder_DecodeFrame(dec, pcm, sizeof(pcm), 0);
                if (AAC_DEC_OK != err)
                {
                    printf("error: aac decode fail %d\n", err);
                    exit(1);
                }
                CStreamInfo *info = aacDecoder_GetStreamInfo(dec);
                if (!info)
                {
                    printf("error: aac decode fail\n");
                    exit(1);
                }
                fwrite(pcm, sizeof(INT_PCM)*info->frameSize*info->numChannels, 1, fout);
#endif
            }
        }
    }

    MP4D_close(&mp4);
    if (input_buf)
        free(input_buf);
    return 0;
}

int main(int argc, char **argv)
{
    // check switches
    int sequential_mode = 0;
    int fragmentation_mode = 0;
    int do_demux = 0;
    int track = 0;
    int i;
    for(i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
            break;
        switch (argv[i][1])
        {
        case 'm': do_demux = 0; break;
        case 'd': do_demux = 1; break;
        case 's': sequential_mode = 1; break;
        case 'f': fragmentation_mode = 1; break;
        case 't': i++; if (i < argc) track = atoi(argv[i]); break;
        default:
            printf("error: unrecognized option\n");
            return 1;
        }
    }
    if (argc <= (i + 1))
    {
        printf("Usage: minimp4 [command] [options] input output\n"
               "Commands:\n"
               "    -m    - do muxing (default); input is h264 elementary stream, output is mp4 file\n"
               "    -d    - do de-muxing; input is mp4 file, output is h264 elementary stream\n"
               "Options:\n"
               "    -s    - enable mux sequential mode (no seek required for writing)\n"
               "    -f    - enable mux fragmentation mode (aka fMP4)\n"
               "    -t    - de-mux tack number\n");
        return 0;
    }
    ssize_t h264_size;
    uint8_t *alloc_buf;
    uint8_t *buf_h264 = alloc_buf = preload(argv[i], &h264_size);
    if (!buf_h264)
    {
        printf("error: can't open h264 file\n");
        exit(1);
    }

    FILE *fout = fopen(argv[i + 1], "wb");
    if (!fout)
    {
        printf("error: can't open output file\n");
        exit(1);
    }

    if (do_demux)
        return demux(alloc_buf, h264_size, fout, track);

    int is_hevc = (0 != strstr(argv[i], "265")) || (0 != strstr(argv[i], "hevc"));

    MP4E_mux_t *mux;
    mp4_h26x_writer_t mp4wr;
    mux = MP4E_open(sequential_mode, fragmentation_mode, fout, write_callback);
    if (MP4E_STATUS_OK != mp4_h26x_write_init(&mp4wr, mux, 352, 288, is_hevc))
    {
        printf("error: mp4_h26x_write_init failed\n");
        exit(1);
    }

#if ENABLE_AUDIO
    ssize_t pcm_size;
    int16_t *alloc_pcm;
    int16_t *buf_pcm  = alloc_pcm = (int16_t *)preload("stream.pcm", &pcm_size);
    if (!buf_pcm)
    {
        printf("error: can't open pcm file\n");
        exit(1);
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

        if (MP4E_STATUS_OK != mp4_h26x_write_nal(&mp4wr, buf_h264, nal_size, 90000/VIDEO_FPS))
        {
            printf("error: mp4_h26x_write_nal failed\n");
            exit(1);
        }
        buf_h264  += nal_size;
        h264_size -= nal_size;

#if ENABLE_AUDIO
        if (fragmentation_mode && !mux->fragments_count)
            continue; /* make sure mp4_h26x_write_nal writes sps/pps, because in fragmentation mode first MP4E_put_sample writes moov with track information and dsi.
                         all tracks dsi must be set (MP4E_set_dsi) before first MP4E_put_sample. */
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

            if (MP4E_STATUS_OK != MP4E_put_sample(mux, audio_track_id, buf, out_args.numOutBytes, 1024*90000/AUDIO_RATE, MP4E_SAMPLE_RANDOM_ACCESS))
            {
                printf("error: MP4E_put_sample failed\n");
                exit(1);
            }
        }
#endif
    }
#if ENABLE_AUDIO
    if (alloc_pcm)
        free(alloc_pcm);
    aacEncClose(&aacenc);
#endif
    if (alloc_buf)
        free(alloc_buf);
    MP4E_close(mux);
    mp4_h26x_write_close(&mp4wr);
    if (fout)
        fclose(fout);
}
