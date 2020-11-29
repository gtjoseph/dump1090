// Part of dump1090, a Mode S message decoder
//
// demod_hirate.h: Hi Rate Mode S demodulator
//
// Copyright (c) 2020 George Joseph <g.devel@wxy78.net>
//
// This file is free software: you may copy, redistribute and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your
// option) any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "dump1090.h"
#include "demod_hirate.h"

#define SYMBOLS_PER_BIT 2
#define BITS_PER_PREAMBLE 8
#define SYMBOLS_PER_PREAMBLE (BITS_PER_PREAMBLE << 1)
#define ADSB_DATA_RATE 1000000
#define ADSB_SYMBOL_RATE (ADSB_DATA_RATE * SYMBOLS_PER_BIT)
#define ERROR_DF (0xff)
#define ERROR_EOB (0xfe)
static uint32_t samples_per_symbol = 0;
static uint32_t samples_per_bit = 0;
static uint32_t samples_per_byte = 0;
static uint32_t samples_per_preamble = 0;

struct demod_context {
    uint32_t threshold;
};

static void reportScore(int score);
static void reportMessage(struct modesMessage *mm, double signal_power, int signal_len);

static uint16_t avgSamples(const uint16_t *buf, const uint32_t next, const uint32_t count)
{
    uint32_t sum = 0;
    uint32_t i;
    for(i = 0; i < count; i++) {
        sum += buf[next + i];
    }
    return sum / count;
}

static uint16_t readBit(const uint16_t *buf, uint32_t *next)
{
    uint16_t ba = 0, bb = 0;
    ba = avgSamples(buf, *next, samples_per_symbol);
    *next += samples_per_symbol;
    bb = avgSamples(buf, *next, samples_per_symbol);
    *next += samples_per_symbol;
    return ba > bb;
}

static uint8_t readByte(const uint16_t *buf, uint32_t *next)
{
    int8_t bit;
    int32_t i;
    uint8_t byte = 0;
    for (i=7 ; i >= 0 ; i--) {
        bit = readBit(buf, next);
        if (bit >= 0) {
            byte |= (bit << i);
            continue;
        }
    }
    return byte;
}

/*
 * The preamble symbols:
 *
 * 0123456789012345
 * 1010000101000000
 * /\/\___/\/\_____
 * ^_^____^_^______
 */

static uint32_t check_preamble(const uint16_t *buf, const uint32_t next_sample)
{
    uint32_t i;
    uint32_t score = 0;
    uint16_t symbols[SYMBOLS_PER_PREAMBLE];

    for (i = 0; i < SYMBOLS_PER_PREAMBLE; i++) {
        symbols[i] = avgSamples(buf, next_sample + samples_per_symbol * i, samples_per_symbol);
    }

    /*
     * Max score is 22.
     * The transition symbols (mark -> space or space -> mark) carry more weight than
     * the all-space symbols.
     */

    score += (symbols[1] < symbols[0]) * 2;
    score += (symbols[2] > symbols[1]) * 2;
    score += (symbols[3] > symbols[2]) * 2;
    score += (symbols[4] < symbols[2]);
    score += (symbols[5] < symbols[2]);
    score += (symbols[6] < symbols[7]) * 2;
    score += (symbols[7] > symbols[8]) * 2;
    score += (symbols[8] < symbols[9]) * 2;
    score += (symbols[9] > symbols[10]) * 2;
    score += (symbols[10] < symbols[9]);
    score += (symbols[11] < symbols[9]);
    score += (symbols[12] < symbols[9]);
    score += (symbols[13] < symbols[9]);
    score += (symbols[14] < symbols[9]);
    score += (symbols[15] < symbols[9]);

    return score;
}

static uint8_t check_df(const uint16_t *buf, const uint32_t next_sample, uint32_t *msg_bytelen)
{
    uint8_t df;
    uint32_t local_next = next_sample;

    df = readByte(buf, &local_next);
    df >>= 3;

    switch (df) {
    case 0: case 4: case 5: case 11:
        *msg_bytelen = MODES_SHORT_MSG_BYTES;
        return df;
    case 16: case 17: case 18: case 20: case 21: case 24:
        *msg_bytelen = MODES_LONG_MSG_BYTES;
        return df;
    default:
        return ERROR_DF;
    }
}

static uint8_t find_preamble(const uint16_t *buf, uint32_t *next_sample, const uint32_t max_sample,
    struct demod_context *ctx)
{
    uint32_t found_preamble = 0;
    uint32_t score = 0;
    uint32_t preamble_start;
    uint32_t i;

    while (*next_sample < max_sample) {

        for (i = 0; i < 3; i++) {
            score += buf[*next_sample + i];
        }
        if (score < ctx->threshold) {
            *next_sample += 2;
            continue;
        }

        score = check_preamble(buf, *next_sample);

        found_preamble = (score >= 20);
        preamble_start = *next_sample;

        if (!found_preamble) {
            if (score >= 18) {
                *next_sample = preamble_start + 1;
            } else if (score >= 10){
                *next_sample = preamble_start + (samples_per_symbol / 3);
            } else {
                *next_sample = preamble_start + (samples_per_symbol / 2);
            }
            continue;
        }

        *next_sample += (samples_per_preamble);
        Modes.stats_current.demod_preambles++;
        return 1;
    }

    return 0;
}

static inline void get_message(const uint16_t *buf, uint32_t *next_sample, uint8_t *outbuf,
    const uint32_t msg_bytelen)
{
    uint32_t i;

    for (i = 0; i < msg_bytelen; i++) {
        outbuf[i] = readByte(buf, next_sample);
    }
}

static void demodulateHiRateTask(void *data)
{

    struct mag_buf *mag = data;
    uint16_t *uin = mag->data;
    uint8_t message_buffer[3][15] = { 0, };
    uint8_t *final_message_buffer;
    int32_t score[3];
    int32_t final_score;

    uint8_t format_code;
    uint32_t next_sample = 0;
    uint32_t preamble_start;
    uint32_t message_start;
    static struct modesMessage zeroMessage;
    struct modesMessage mm;
    uint64_t sum_scaled_signal_power = 0;
    uint32_t mlen = mag->validLength - mag->overlap - 1;
    uint32_t msg_bytelen = MODES_LONG_MSG_BYTES;
    uint32_t msg_bitlen = MODES_LONG_MSG_BYTES << 3;
    uint32_t msg_samplelen = msg_bitlen * samples_per_bit;
    double sum_signal_power;
    double signal_power;
    uint64_t scaled_signal_power = 0;
    struct demod_context ctx = {0, };
    uint32_t k;

//    ctx.threshold = sqrt(Modes.stats_current.noise_power_sum / Modes.stats_current.noise_power_count) * MAX_AMPLITUDE;
    ctx.threshold = (mag->mean_level * MAX_AMPLITUDE);
    ctx.threshold *= 1.15;

    for (k = 0; k < mlen; k++) {
        if (uin[k] <= ctx.threshold) {
            uin[k] = 0;
        } else {
            uin[k] -= ctx.threshold;
        }
    }

    while (next_sample < mlen) {

        /* Scan forward in the buffer until we find a valid preamble or run out of buffer */
        if (!find_preamble(uin, &next_sample, mlen, &ctx)) {
            break;
        }

        /*
         * find_preamble will have adjusted next_sample to point to the
         * next sample after the preamble
         */
        preamble_start = next_sample - samples_per_preamble;
        message_start = next_sample;

        /*
         * Statistically, if the DF code pointed to by next_sample
         * isn't valid, backing up a sample at a time can increase
         * the chances of detecting a valid one with minimal
         * overhead.
         */
        format_code = check_df(uin, next_sample, &msg_bytelen);
        if (format_code == ERROR_DF) {
            next_sample -= 1;
            format_code = check_df(uin, next_sample, &msg_bytelen);
        }
        if (format_code == ERROR_DF) {
            next_sample -= 1;
            format_code = check_df(uin, next_sample, &msg_bytelen);
        }
        /*
         * We've backed up twice to no avail.
         * Restart the preamble search 2 samples after the start of
         * the current preamble.
         */
        if (format_code == ERROR_DF) {
            next_sample = preamble_start + 2;
            continue;
        }

        /*
         * If we got the DF code, then we also know the message length.
         */
        msg_bitlen = msg_bytelen << 3;
        msg_samplelen = msg_bitlen * samples_per_bit;

        score[1] = -100;
        score[2] = -100;

        /*
         * Like the DF code, we can get a better decode rate by
         * scoring the message 3 times and taking the best one.
         * In this case though, we get better results by moving
         * forward in the sample buffer rather than backing up.
         */
        get_message(uin, &next_sample, message_buffer[0], msg_bytelen);
        score[0] = scoreModesMessage(message_buffer[0], msg_bitlen);

        next_sample = message_start + 1;
        get_message(uin, &next_sample, message_buffer[1], msg_bytelen);
        score[1] = scoreModesMessage(message_buffer[1], msg_bitlen);
/*
        next_sample = message_start + 2;
        get_message(uin, &next_sample, message_buffer[2], msg_bytelen);
        score[2] = scoreModesMessage(message_buffer[2], msg_bitlen);
*/
#define BEST_SCORE(__score) \
({ \
    int32_t __m = 0, __i, __fi = 0; \
    for (__i = 0; __i < 3; __i++) { \
        if (__score[__i] > __m) { \
            __m = __score[__i]; \
            __fi = __i; \
        } \
    } \
    (__fi); \
})

        int32_t best_score_ix = BEST_SCORE(score);
        final_score = score[best_score_ix];
        final_message_buffer = message_buffer[best_score_ix];
        next_sample = message_start + best_score_ix + msg_samplelen - samples_per_byte;

        if (final_score < 0) {
            reportScore(final_score);
            /* Back to where we started, move forward 1 sample, then search for preamble again */
            next_sample = preamble_start + 1;
            continue;
        }

        // Set initial mm structure details
        mm = zeroMessage;

        // For consistency with how the Beast / Radarcape does it,
        // we report the timestamp at the end of bit 56 (even if
        // the frame is a 112-bit frame)
        mm.timestampMsg = mag->sampleTimestamp + (preamble_start / samples_per_bit) + (8 + 56) * 12;

        // compute message receive time as block-start-time + difference in the 12MHz clock
        mm.sysTimestampMsg = mag->sysTimestamp + receiveclock_ms_elapsed(mag->sampleTimestamp, mm.timestampMsg);

        mm.score = final_score;

        final_score = decodeModesMessage(&mm, final_message_buffer);
        if (final_score < 0) {
            /*
             * This is going to be very rare since we already got a good score.
             * If we fail now, we probably won't do any better on a retry
             * so just toss the message and look for the next one.
             */
            reportScore(final_score);
            continue;
        }

        // measure signal power
        scaled_signal_power = 0;
        for (k = 0; k < msg_samplelen; ++k) {
            uint32_t mag = uin[message_start + k] + ctx.threshold;
            scaled_signal_power += mag * mag;
        }

        signal_power = scaled_signal_power / MAX_AMPLITUDE / MAX_AMPLITUDE;
        mm.signalLevel = signal_power / msg_samplelen;
        sum_scaled_signal_power += scaled_signal_power;

        reportMessage(&mm, signal_power, msg_samplelen);
    }

    sum_signal_power = sum_scaled_signal_power / MAX_AMPLITUDE / MAX_AMPLITUDE;

    Modes.stats_current.noise_power_count += mlen;
    Modes.stats_current.samples_processed += mlen;
    Modes.stats_current.samples_dropped += mag->dropped;
    Modes.stats_current.noise_power_sum += ((mag->mean_power * mlen) - sum_signal_power);

}

static void reportScore(int score)
{
    if (score == -1) {
        Modes.stats_current.demod_rejected_unknown_icao++;
    } else {
        Modes.stats_current.demod_rejected_bad++;
    }
}

static void reportMessage(struct modesMessage *mm, double signal_power, int signal_len)
{
    Modes.stats_current.demod_accepted[mm->correctedbits]++;
    Modes.stats_current.signal_power_count += signal_len;
    if (mm->signalLevel > 0.50119) {
        Modes.stats_current.strong_signal_count++; // signal power above -3dBFS
    }

    Modes.stats_current.signal_power_sum += signal_power;
    if (mm->signalLevel > Modes.stats_current.peak_signal_power) {
        Modes.stats_current.peak_signal_power = mm->signalLevel;
    }
    useModesMessage(mm);
}

void demodulateHiRate(struct mag_buf *mag)
{
    struct timespec start_time;

    start_cpu_timing(&start_time);
    demodulateHiRateTask(mag);
    end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);
    fifo_release(mag);
}

void demodulateHiRateInit(void *context)
{
    MODES_NOTUSED(context);

    samples_per_symbol = (const uint32_t)(Modes.sample_rate / ADSB_SYMBOL_RATE);
    samples_per_bit = samples_per_symbol * SYMBOLS_PER_BIT;
    samples_per_byte = samples_per_bit * 8;
    samples_per_preamble = BITS_PER_PREAMBLE * samples_per_bit;
}

void demodulateHiRateFree(void *context)
{
    MODES_NOTUSED(context);
}
