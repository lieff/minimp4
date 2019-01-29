#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

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

static void write_callback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    fwrite(buffer, size, 1, f);
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
    MP4E_mux_t *mux;
    mp4_h26x_writer_t mp4wr;
    mux = MP4E__open(sequential_mode, fout, write_callback);
    mp4_h26x_write_init(&mp4wr, mux, 352, 288, is_hevc);

    while (h264_size > 0)
    {
        ssize_t nal_size = get_nal_size(buf_h264, h264_size);
        //printf("nal size=%ld, rest=%ld\n", nal_size, h264_size);
        if (!nal_size)
        {
            buf_h264  += 1;
            h264_size -= 1;
            continue;
        }

        mp4_h26x_write_nal(&mp4wr, buf_h264, nal_size, 90000/VIDEO_FPS);
        buf_h264  += nal_size;
        h264_size -= nal_size;
    }
    if (alloc_buf)
        free(alloc_buf);
    MP4E__close(mux);
    if (fout)
        fclose(fout);
}
