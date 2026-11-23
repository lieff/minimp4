/*
 * Regression test: tfdt fragment timeline under variable sample durations
 * (VFR) in fragmentation mode.
 *
 * MP4E_put_sample used to derive the tfdt base media decode time as
 * fragments_count * duration, which is only correct for a constant
 * duration. With VFR durations consecutive fragments overlap or leave
 * gaps, breaking seek in players.
 *
 * Muxes fragments with jittering durations, then scans the produced moof
 * boxes and verifies tfdt[n+1] == tfdt[n] + duration[n] for every n.
 *
 * Build: cc -O2 -DMP4D_TFDT_SUPPORT=1 test_tfdt_vfr.c -o test_tfdt_vfr
 * Run:   ./test_tfdt_vfr
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#define SAMPLES 900
#define TIMESCALE 90000

static unsigned char *g_buf;
static size_t g_size, g_cap;

static int write_callback(int64_t offset, const void *buffer, size_t size, void *token)
{
    (void)token;
    if ((size_t)offset + size > g_cap)
    {
        unsigned char *p;
        g_cap = ((size_t)offset + size) * 2;
        p = (unsigned char *)realloc(g_buf, g_cap);
        if (!p)
            exit(1);
        g_buf = p;
    }
    memcpy(g_buf + offset, buffer, size);
    if ((size_t)offset + size > g_size)
        g_size = (size_t)offset + size;
    return 0;
}

static unsigned rd32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static unsigned long long rd64(const unsigned char *p)
{
    return ((unsigned long long)rd32(p) << 32) | rd32(p + 4);
}

/* Locate the tfdt and the trun sample_duration inside one moof box. */
static int parse_moof(const unsigned char *moof, size_t size,
                      unsigned long long *tfdt, unsigned *duration)
{
    size_t pos = 8; /* skip moof header */
    *tfdt = 0;
    *duration = 0;
    while (pos + 8 <= size)
    {
        unsigned box_size = rd32(moof + pos);
        unsigned box_type = rd32(moof + pos + 4);
        if (box_size < 8 || pos + box_size > size)
            return 0;
        if (box_type == 0x74726166) /* traf */
        {
            size_t tpos = pos + 8;
            while (tpos + 8 <= pos + box_size)
            {
                unsigned tsize = rd32(moof + tpos);
                unsigned ttype = rd32(moof + tpos + 4);
                if (tsize < 8 || tpos + tsize > pos + box_size)
                    return 0;
                if (ttype == 0x74666474) /* tfdt */
                {
                    if (moof[tpos + 8] == 1 && tsize >= 20)
                        *tfdt = rd64(moof + tpos + 12);
                    else if (tsize >= 16)
                        *tfdt = rd32(moof + tpos + 12);
                }
                else if (ttype == 0x7472756e) /* trun */
                {
                    unsigned flags = rd32(moof + tpos + 8) & 0xffffff;
                    const unsigned char *f = moof + tpos + 16;
                    if (flags & 0x001) f += 4; /* data_offset */
                    if (flags & 0x004) f += 4; /* first_sample_flags */
                    if (flags & 0x100)         /* sample_duration_present */
                        *duration = rd32(f);
                }
                tpos += tsize;
            }
        }
        pos += box_size;
    }
    return *duration != 0;
}

int main(void)
{
    MP4E_mux_t *mux;
    MP4E_track_t tr;
    unsigned char frame[64];
    unsigned char sps[] = {0x67, 0x42, 0xc0, 0x0a, 0xd9, 0x05, 0x87, 0xe5, 0xc0, 0x44,
                           0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xf0,
                           0x3c, 0x50, 0xa6, 0x44};
    unsigned char pps[] = {0x68, 0xce, 0x3c, 0x80};
    unsigned i, n_moof = 0;
    unsigned long long expected = 0, total = 0;
    int failed = 0;

    memset(frame, 0, sizeof(frame));
    frame[3] = 1;   /* AnnexB start code */
    frame[4] = 0x41;

    mux = MP4E_open(1 /*sequential*/, 1 /*fragmented*/, NULL, write_callback);
    memset(&tr, 0, sizeof(tr));
    tr.track_media_kind       = e_video;
    tr.object_type_indication = MP4_OBJECT_TYPE_AVC;
    tr.time_scale             = TIMESCALE;
    tr.u.v.width              = 64;
    tr.u.v.height             = 64;
    {
        int track = MP4E_add_track(mux, &tr);
        if (track < 0)
        {
            printf("FAIL: MP4E_add_track\n");
            return 1;
        }
        MP4E_set_sps(mux, track, sps, sizeof(sps));
        MP4E_set_pps(mux, track, pps, sizeof(pps));
    }

    for (i = 0; i < SAMPLES; i++)
    {
        /* 2999/3000/3001 jitter: deltas of +-1 tick are enough to corrupt
         * the old fragments_count * duration formula. */
        unsigned duration = 2999 + (i % 3);
        total += duration;
        if (MP4E_put_sample(mux, 0, frame, sizeof(frame), (int)duration,
                            (i % 300 == 0) ? MP4E_SAMPLE_RANDOM_ACCESS
                                           : MP4E_SAMPLE_DEFAULT) != MP4E_STATUS_OK)
        {
            printf("FAIL: MP4E_put_sample at %u\n", i);
            return 1;
        }
    }
    MP4E_close(mux);

    /* Scan the produced moof boxes and verify the timeline is gapless. */
    {
        size_t pos = 0;
        while (pos + 8 <= g_size && n_moof < SAMPLES)
        {
            unsigned box_size = rd32(g_buf + pos);
            unsigned box_type = rd32(g_buf + pos + 4);
            unsigned long long tfdt;
            unsigned duration;
            if (box_size < 8 || pos + box_size > g_size)
                break;
            if (box_type == 0x6d6f6f66 /* moof */ &&
                parse_moof(g_buf + pos, box_size, &tfdt, &duration))
            {
                if (n_moof == 0)
                {
                    if (tfdt != 0)
                    {
                        printf("FAIL: first tfdt %llu != 0\n", tfdt);
                        failed = 1;
                    }
                }
                else if (tfdt != expected)
                {
                    printf("FAIL: fragment %u tfdt %llu, expected %llu (gap %lld)\n",
                           n_moof, tfdt, expected, (long long)tfdt - (long long)expected);
                    failed = 1;
                }
                expected = tfdt + duration;
                n_moof++;
            }
            pos += box_size;
        }
    }

    if (!failed && n_moof != SAMPLES)
    {
        printf("FAIL: parsed %u moof boxes, expected %u\n", n_moof, SAMPLES);
        failed = 1;
    }
    if (!failed && expected != total)
    {
        printf("FAIL: timeline end %llu != sum of durations %llu\n", expected, total);
        failed = 1;
    }
    if (!failed)
        printf("PASS: %u fragments, gapless timeline 0..%llu ticks\n", n_moof, expected);

    free(g_buf);
    return failed;
}
