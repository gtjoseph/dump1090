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
#include <assert.h>

#define SYMBOLS_PER_BIT 2
#define BITS_PER_PREAMBLE 8
#define SYMBOLS_PER_PREAMBLE (BITS_PER_PREAMBLE << 1)
#define ADSB_DATA_RATE 1000000
#define ADSB_SYMBOL_RATE (ADSB_DATA_RATE * SYMBOLS_PER_BIT)
#define ERROR_DF (0xff)
#define ERROR_EOB (0xfe)
#define SAMPLE_SCALE 65535.0f
static uint32_t samples_per_symbol = 0;
static uint32_t samples_per_bit = 0;
static uint32_t samples_per_byte = 0;
static uint32_t samples_per_preamble = 0;

struct demod_context {
    size_t buffer_mean;
};

static void reportScore(int score);
static void reportMessage(struct modesMessage *mm, double signal_power, int signal_len);

#define avgSamples(__buf, __next, __count) \
({ \
    size_t __sum = 0; \
    uint32_t __i; \
    for(__i = 0; __i < (__count); __i++) { \
    	uint16_t s = __buf[(__next) + __i]; \
        __sum += s; \
    } \
    (__sum / ((__count))); \
})

#define avgMidSamples(__buf, __next, __count) \
({ \
    size_t __sum = 0; \
    uint32_t __i; \
    for(__i = 1; __i < (__count) - 2; __i++) { \
        uint16_t s = __buf[(__next) + __i]; \
        __sum += s; \
    } \
    (__sum / ((__count) - 2)); \
})


#define readBit(__buf, __next) \
({ \
    uint16_t ba = 0, bb = 0; \
    ba = avgSamples(__buf, (*(__next)), samples_per_symbol); \
    (*(__next)) += samples_per_symbol; \
    bb = avgSamples(__buf, (*(__next)), samples_per_symbol); \
    (*(__next)) += samples_per_symbol; \
    (ba > bb); \
})

#define readByte(__buf, __next) \
({ \
    int8_t __bit; \
    int32_t __i; \
    uint8_t __byte = 0; \
    for (__i=7 ; __i >= 0 ; __i--) { \
        __bit = readBit(__buf, (__next)); \
        if (__bit >= 0) { \
            __byte |= (__bit << __i); \
            continue; \
        } \
    } \
    (__byte); \
})

//0123456789012345
//1010000101000000

static  inline uint32_t check_preamble(uint16_t *buf, size_t next_sample)
{
    uint32_t i;
    uint32_t score = 0;
    uint16_t symbols[SYMBOLS_PER_PREAMBLE];


    for (i = 0; i < SYMBOLS_PER_PREAMBLE; i++) {
        symbols[i] = avgSamples(buf, next_sample + samples_per_symbol * i, samples_per_symbol);
    }

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

static  inline  __attribute__((unused)) uint8_t check_df(uint16_t *buf, size_t next_sample, size_t *msg_bytelen)
{
    uint8_t df;
    size_t local_next = next_sample;

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

static inline uint8_t find_preamble(uint16_t *buf, size_t *next_sample, size_t max_sample,
    __attribute__((unused)) struct demod_context *ctx)
{
    uint32_t found_preamble = 0;
    uint32_t score = 0;
    size_t preamble_start;

    while (*next_sample < max_sample) {
#if 1
        uint32_t i = 0;
        for (i = 0; i < 4; i++) {
            score += buf[*next_sample + i];
        }
        if (score < ctx->buffer_mean) {
            *next_sample += 3;
            continue;
        }

#endif
        score = check_preamble(buf, *next_sample);

        found_preamble = (score >= 18);
        preamble_start = *next_sample;

        if (!found_preamble) {
            if (score >= 17) {
                *next_sample = preamble_start + 1;
            } else if (score >= 10){
                *next_sample = preamble_start + (samples_per_symbol / 2);
            } else {
                *next_sample = preamble_start + (samples_per_symbol);
            }
            continue;
        }

        *next_sample += (samples_per_preamble);
        Modes.stats_current.demod_preambles++;
        return 0;
    }

    return ERROR_EOB;
}

static void get_message(uint16_t *buf, size_t *next_sample, uint8_t *outbuf, size_t msg_bytelen)
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
    uint8_t message_buffer[15] = { 0, };
    uint8_t format_code;
    size_t next_sample = 0;
    size_t preamble_start;
    size_t message_start;
    int32_t score;
    static struct modesMessage zeroMessage;
    struct modesMessage mm;
    uint64_t sum_scaled_signal_power = 0;
    size_t mlen = mag->validLength - mag->overlap;
    struct timespec start_time;
    size_t msg_bytelen = MODES_LONG_MSG_BYTES;
    size_t msg_bitlen = MODES_LONG_MSG_BYTES * 8;
    size_t msg_samplelen = msg_bitlen * samples_per_bit;
    double sum_signal_power;
    double signal_power;
    uint64_t scaled_signal_power = 0;
    struct demod_context ctx = {0, };
    size_t k;


    start_cpu_timing(&start_time);
    ctx.buffer_mean = mag->mean_level * SAMPLE_SCALE;

    for (k = 0; k < mlen; k++) {
        if (uin[k] <= ctx.buffer_mean * 2) {
            uin[k] = 0;
        } else {
            uin[k] -= ctx.buffer_mean;
        }
    }

    while (next_sample < mlen) {

        format_code = find_preamble(uin, &next_sample, mlen, &ctx);
        if (format_code == ERROR_EOB) {
            break;
        }

        preamble_start = next_sample - samples_per_preamble;
        message_start = next_sample;

        format_code = check_df(uin, next_sample, &msg_bytelen);
        if (format_code == ERROR_DF) {
            next_sample -= 1;
            format_code = check_df(uin, next_sample, &msg_bytelen);
        }
        if (format_code == ERROR_DF) {
            next_sample -= 1;
            format_code = check_df(uin, next_sample, &msg_bytelen);
        }
        if (format_code == ERROR_DF) {
            next_sample = preamble_start + 2;
            continue;
        }

        get_message(uin, &next_sample, message_buffer, msg_bytelen);

        msg_bitlen = msg_bytelen * 8;
        msg_samplelen = msg_bitlen * samples_per_bit;
        next_sample = message_start + msg_samplelen - samples_per_byte;

        score = scoreModesMessage(message_buffer, msg_bitlen);

#if 1
        if (score < 0) {
            next_sample = message_start - 1;
            get_message(uin, &next_sample, message_buffer, msg_bytelen);
            score = scoreModesMessage(message_buffer, msg_bitlen);
        }
        if (score < 0) {
            next_sample = message_start - 2;
            get_message(uin, &next_sample, message_buffer, msg_bytelen);
            score = scoreModesMessage(message_buffer, msg_bytelen * 8);
        }
#endif

        if (score < 0) {
            reportScore(score);
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

        mm.score = score;

        score = decodeModesMessage(&mm, message_buffer);

        if (score < 0) {
            reportScore(score);
            next_sample = preamble_start + 1;
            continue;
        }

        // measure signal power
        scaled_signal_power = 0;
        for (k = 0; k < msg_samplelen; ++k) {
            uint32_t mag = uin[message_start + k];
            scaled_signal_power += mag * mag;
        }

        signal_power = scaled_signal_power / SAMPLE_SCALE / SAMPLE_SCALE;
        mm.signalLevel = signal_power / msg_samplelen;
        sum_scaled_signal_power += scaled_signal_power;

        reportMessage(&mm, signal_power, msg_samplelen);
    }

    sum_signal_power = sum_scaled_signal_power / SAMPLE_SCALE / SAMPLE_SCALE;

    // These are atomic_ints and don't require the mutex
    Modes.stats_current.noise_power_count += mlen;
    Modes.stats_current.samples_processed += mag->validLength;
    Modes.stats_current.samples_dropped += mag->dropped;
    Modes.stats_current.noise_power_sum += (mag->mean_power * mlen - sum_signal_power);
    end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);

    fifo_release(mag);

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
    // These are atomic_ints and don't require the mutex
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
    demodulateHiRateTask(mag);
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
