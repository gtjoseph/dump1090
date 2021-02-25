/* -*- c -*- */
#ifdef IN_EDITOR
#include "dump1090.h"
#include "demod.h"
#include "demod_multi.h"

static demodulator_context_t *ctx;

#define DEMOD_FN demodulateMulti12000
#define DEMOD_SAMPLES_PER_HALFBIT 6
#endif

static void DEMOD_FN (struct mag_buf *mag)
{

    uint32_t message_count = 0;

    /* precompute average of magnitude data */
    static uint16_t *averaged = NULL;
    static unsigned averaged_allocated = 0;
    const unsigned averaged_avail = mag->validLength - ctx->smoother_window + 1;
    if (!averaged || averaged_allocated < averaged_avail) {
        free(averaged);
        averaged = malloc(averaged_avail * sizeof(uint16_t));
        averaged_allocated = averaged_avail;
    }
    starch_boxcar_u16(mag->data, mag->validLength, ctx->smoother_window, averaged);

    /* precompute preamble correlation */
    static uint16_t *correlated = NULL;
    static unsigned correlated_allocated = 0;
    const unsigned correlated_avail = averaged_avail - DEMOD_SAMPLES_PER_HALFBIT * 9;
    if (!correlated || correlated_allocated < correlated_avail) {
        free(correlated);
        correlated = malloc(correlated_avail * sizeof(uint16_t));
        correlated_allocated = correlated_avail;
    }
    starch_preamble_u16(averaged, averaged_avail, DEMOD_SAMPLES_PER_HALFBIT, correlated);

    /* set threshold from mean of averaged data */
    double mean_level, mean_power;
    starch_mean_power_u16(averaged, averaged_avail, &mean_level, &mean_power);
    const uint32_t threshold = 65536.0 * mean_level * ctx->preamble_threshold;

    const unsigned limit = mag->validLength - mag->overlap;

    assert(correlated_avail >= limit);

    for (unsigned j = 0; j < limit; j++) {
        const uint16_t *sc = &correlated[j];
        const uint16_t *sa = &averaged[j];

        /* threshold detect against correlation score */
        if (sc[P1] < threshold)
            continue;

        unsigned best = 0;

        uint16_t preamble_avg_mark;
        uint16_t preamble_avg_space;

        if (demodCheckPreamble(sa, sc, ctx,
            &preamble_avg_mark, &preamble_avg_space, &best)) {
            continue;
        }

        /* good candidate, do the demodulation */
        Modes.stats_current.demod_preambles++;

        unsigned char rawbytes[MODES_LONG_MSG_BYTES];

        const uint16_t *data = &sa[best + DEMOD_SAMPLES_PER_HALFBIT * 16];
        for (unsigned byteNum = 0; byteNum < sizeof(rawbytes); ++byteNum) {
            unsigned value = 0;
            for (unsigned bitNum = 0; bitNum < 8; ++bitNum) {
                value <<= 1;
                if (data[0] > data[DEMOD_SAMPLES_PER_HALFBIT])
                    value |= 1;
                data += DEMOD_SAMPLES_PER_HALFBIT * 2;
            }
            rawbytes[byteNum] = value;
        }

        int score = scoreModesMessage(rawbytes);
        if (score < SR_ACCEPT_THRESHOLD) {
            if (score >= SR_UNKNOWN_THRESHOLD)
                Modes.stats_current.demod_rejected_unknown_icao++;
            else
                Modes.stats_current.demod_rejected_bad++;
            continue; // nope.
        }

        /* really demodulate it */
        struct modesMessage mm;
        memset(&mm, 0, sizeof(mm));

        mm.timestampMsg = mag->sampleTimestamp + (j + best) * (6 / DEMOD_SAMPLES_PER_HALFBIT) + (8 + 56) * 12;
        mm.sysTimestampMsg = mag->sysTimestamp + receiveclock_ms_elapsed(mag->sampleTimestamp, mm.timestampMsg);
        mm.score = score;

        int result = decodeModesMessage(&mm, rawbytes);
        if (result < 0) {
            abort();
            if (result == -1)
                Modes.stats_current.demod_rejected_unknown_icao++;
            else
                Modes.stats_current.demod_rejected_bad++;
            j += DEMOD_SAMPLES_PER_HALFBIT;
            continue;
        }

        if (Modes.stats) {
            ctx->decode_distro[DISTRO_OFFSET(0)]++;
        }

        Modes.stats_current.demod_accepted[mm.correctedbits]++;

        processSignalAndNoise(&mm, preamble_avg_mark, preamble_avg_space);

        message_count++;
        useModesMessage(&mm);

        /* skip to next message (ish) */
        j += 16 * DEMOD_SAMPLES_PER_HALFBIT              /* preamble */
            + 2 * DEMOD_SAMPLES_PER_HALFBIT * mm.msgbits /* data */
            - 8 * DEMOD_SAMPLES_PER_HALFBIT;             /* back up a bit, sometimes we can handle preambles that overlap the previous message */
    }

    /* There were no messages found in the buffer so add the mean_power to the noise */
    if (!message_count) {
        Modes.stats_current.noise_power_sum += mag->mean_power;
        Modes.stats_current.noise_power_count += 1;
    }


}
