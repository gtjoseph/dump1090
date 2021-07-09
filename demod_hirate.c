// Part of dump1090, a Mode S message decoder
//
// demod_hirate.c: HiRate Mode S demodulator
//
// Copyright (c) 2020 George Joseph <g.devel@wxy78.net>
//
// Preamble detection code by Oliver Jowitt
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
#include "demod.h"
#include "demod_hirate.h"
#include <inttypes.h>
#include <stdlib.h>

static demodulator_context_t *ctx;

struct message_context {
    uint32_t    threshold;
    uint16_t    preamble_avg_mark;
    uint16_t    preamble_avg_space;
    uint16_t    preamble_mark_limit_high;
    uint16_t    preamble_mark_limit_low;
    uint32_t    preamble_sample_offset;

    int32_t     msg_sample_offset;
    uint32_t    msg_samplelen;
    uint8_t     msg[MODES_LONG_MSG_BYTES];
};

/*!
 * @brief Reaad 1 byte from the buffer
 *
 * @param buf input buffer
 * @param msg message context
 * @return byte read
 *
 */
static uint8_t readByte(const uint16_t *buf, struct message_context *msg)
{
    MODES_NOTUSED(msg);
    uint8_t byte = 0;
    int8_t bit;
    int32_t i;
    uint32_t n = 0;
    bool ba;
    bool bb;

    for (i=7 ; i >= 0 ; i--) {
        if (ctx->no_mark_limits) {
            bit = (buf[n] > buf[n + ctx->samples_per_symbol]);
        } else {
            ba  = ((buf[n] > msg->preamble_mark_limit_low) && (buf[n] < msg->preamble_mark_limit_high));
            bb = ((buf[n + ctx->samples_per_symbol] > msg->preamble_mark_limit_low) && (buf[n + ctx->samples_per_symbol] < msg->preamble_mark_limit_high));

            if(ba && !bb) {
                bit = 1;
            } else if(bb && !ba) {
                bit = 0;
            } else {
                bit = buf[n] > buf[n + ctx->samples_per_symbol];
            }
        }
        n += ctx->samples_per_bit;
        byte |= (bit << i);
    }
    return byte;
}

/*!
 * @brief Read an arbitrary number of bytes from the buffer
 *
 * @param buf input buffer
 * @param outbuf pointer to the output buffer
 * @param count number of bytes to read
 * @param msg message context
 */
static void readBytes(const uint16_t *buf, uint8_t *outbuf,
    const uint32_t count, struct message_context *msg)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        outbuf[i] = readByte(buf + ctx->sample_byte_offsets[i], msg);
    }
}

/*!
 * @brief Check the provided DF code to see if it's valid
 *
 * @param df df code
 * @return length of message in bytes
 *
 */
static uint32_t check_df(const uint8_t df)
{
    switch (df) {
    case 0: case 4: case 5: case 11:
        return MODES_SHORT_MSG_BYTES;
    case 16: case 17: case 18: case 20: case 21: case 24:
        return MODES_LONG_MSG_BYTES;
    default:
        return 0;
    }
}

/*!
 * @brief Read a message from the buffer
 *
 * @param buf input buffer
 * @param next position in buffer to read
 * @param msg message context
 * @return score
 */
static int32_t get_message(const uint16_t *buf, struct message_context *msg)
{
    int32_t i;
    int32_t score = SR_NOT_SET;
    uint8_t df_byte;
    uint32_t msg_bytelen = 0;

    for (i = ctx->demod_window_low; i <= ctx->demod_window_high; i++) {
        /*
         * Fast Fail.  If we don't have a valid DF, don't bother reading the rest of the message.
         */
        df_byte = readByte(buf + i, msg);
        msg_bytelen = check_df(df_byte >> 3);
        if (msg_bytelen == 0) {
            score = SR_UNKNOWN_DF;
            continue;
        }

        readBytes(buf + i, msg->msg, msg_bytelen, msg);

        score = scoreModesMessage(msg->msg);
        if (score >= SR_ACCEPT_THRESHOLD) {
            msg->msg_sample_offset = i;
            msg->msg_samplelen = msg_bytelen * ctx->samples_per_byte;
            break;
        }
    }

    return score;
}

/*!
 * @brief Main demodulator task
 *
 * @param data mag_buf from core
 */
static void demodulateHiRateTask(struct mag_buf *mag)
{
    uint32_t mlen = mag->validLength - mag->overlap;
    uint32_t message_start;
    struct message_context msg_ctx;
    int32_t score;
    struct modesMessage mm;
    uint32_t message_count = 0;
    uint32_t j;

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
    const unsigned correlated_avail = averaged_avail - ctx->samples_per_symbol * 9;
    if (!correlated || correlated_allocated < correlated_avail) {
        free(correlated);
        correlated = malloc(correlated_avail * sizeof(uint16_t));
        correlated_allocated = correlated_avail;
    }
    starch_preamble_u16(averaged, averaged_avail, ctx->samples_per_symbol, correlated);

    /* set threshold from mean of averaged data */
    double mean_level, mean_power;
    starch_mean_power_u16(averaged, averaged_avail, &mean_level, &mean_power);
    const uint32_t threshold = 65536.0 * mean_level * ctx->preamble_threshold;

    for (j = 0; j < mlen; j++) {
        const uint16_t *sa = &averaged[j];
        const uint16_t *sc = &correlated[j];

        if (sc[P1] < threshold) {
            continue;
        }

        /* Is there a valid preamble? */
        if (demodCheckPreamble(sa, sc, ctx,
            &msg_ctx.preamble_avg_mark, &msg_ctx.preamble_avg_space, &msg_ctx.preamble_sample_offset)) {
            continue;
        }

        /* We got one with a reasonable certainty */
        Modes.stats_current.demod_preambles++;

        /*
         * Initially, message_start is set to the calculated location
         * in the buffer.
         */
        msg_ctx.preamble_mark_limit_high = msg_ctx.preamble_avg_mark * 1.414;
        msg_ctx.preamble_mark_limit_low = msg_ctx.preamble_avg_mark * 0.707;

        message_start = msg_ctx.preamble_sample_offset + ctx->samples_per_preamble;

        score = get_message(&sa[message_start], &msg_ctx);

        if (score < SR_ACCEPT_THRESHOLD) {
            if (score >= SR_UNKNOWN_THRESHOLD)
                Modes.stats_current.demod_rejected_unknown_icao++;
            else
                Modes.stats_current.demod_rejected_bad++;
            continue; // nope.
        }

        // Set initial mm structure details
        memset(&mm, 0, sizeof(mm));

        /*
         * For consistency with how the Beast / Radarcape does it,
         * we report the timestamp at the end of bit 56 (even if
         * the frame is a 112-bit frame)
         */

        uint32_t end_of_message_sample =
        /*
         *  Offset from start of mag buffer
         *  |   Offset to best preamble           Offset to end of preamble   Offset to best message
         *  -    ------------------------------   -------------------------   ---------------------
         */
            j + msg_ctx.preamble_sample_offset + ctx->samples_per_preamble + msg_ctx.msg_sample_offset
        /*    Offset to end of message */
            + (ctx->samples_per_bit * 56);

        /*
         * Since timestamps are measured by 12MHz clock cycles and the ADSB bit rate is 1Mb/s
         *  we could take end_of_message_sample and divide by samples_per_bit then multiply
         *  by 12 but if we do that, the first division will cause us to lose precision.
         *  Instead, we multiply end_of_message_sample by 12 first, then divide by samples_per_bit.
         */
        mm.timestampMsg = mag->sampleTimestamp + (end_of_message_sample * 12 / ctx->samples_per_bit);

        /* compute message receive time as block-start-time + difference in the 12MHz clock */
        mm.sysTimestampMsg = mag->sysTimestamp + receiveclock_ms_elapsed(mag->sampleTimestamp, mm.timestampMsg);

        mm.score = score;

        score = decodeModesMessage(&mm, msg_ctx.msg);
        if (score < 0) {
            if (score == -1) {
                Modes.stats_current.demod_rejected_unknown_icao++;
            } else if (score == -3) {
                Modes.stats_current.demod_rejected_dup++;
            } else {
                Modes.stats_current.demod_rejected_bad++;
            }
            continue;
        }

        message_count++;
        if (Modes.stats) {
            Modes.stats_current.demod_decode_distro[DISTRO_OFFSET(msg_ctx.msg_sample_offset)]++;
        }

        Modes.stats_current.demod_accepted[mm.correctedbits]++;

        processSignalAndNoise(&mm, msg_ctx.preamble_avg_mark, msg_ctx.preamble_avg_space);

        /* That's it.  Use the message. */
        useModesMessage(&mm);

        /* skip to next message (ish) */
        j += msg_ctx.preamble_sample_offset + ctx->samples_per_preamble       /* preamble */
            + msg_ctx.msg_samplelen  /* data */
            - ctx->sample_symbol_offsets[8];         /* back up a bit, sometimes we can handle preambles that overlap the previous message */
    }

    /* There were no messages found in the message so add the mean_power to the noise */
    if (!message_count) {
        Modes.stats_current.noise_power_sum += mag->mean_power;
        Modes.stats_current.noise_power_count += 1;
    }

}

/*!
 * @brief Public demodulate function called by the core
 *
 * @param mag mag_buf from core
 */
void demodulateHiRate(struct mag_buf *mag)
{
    struct timespec start_time;

    start_cpu_timing(&start_time);
    demodulateHiRateTask(mag);
    Modes.stats_current.samples_processed += mag->validLength - mag->overlap;
    Modes.stats_current.samples_dropped += mag->dropped;

    end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);
    fifo_release(mag);
}

/*!
 * @brief Demodulator initialization
 * @param context (not used)
 */
int demodulateHiRateInit(demodulator_t *demod)
{
    ctx = demod->ctx;

    return 0;
}

/*!
 * Demodulator cleanup
 * @param context (not used)
 */
void demodulateHiRateFree()
{

}

/*!
 * @brief Print demodulator options help
 */
void demodulateHiRateHelp(void)
{
    printf("      HiRate Demodulator specific options (use with --demod hirate)\n");

    printf("\n");
    printf("--demod-msg-window <l>:<h>         l: Offset from the end of the preamble to start the\n");
    printf("                                   search for a message.  May be negative.\n");
    printf("                                   h: Offset from the end of the preamble to stop the\n");
    printf("                                   search for a message.\n");
    printf("                                   Inclusive window must not exceed %d.\n", DEMOD_MAX_WINDOW_WIDTH);
    printf("                                   The default is %d:%d\n", DEFAULT_DEMOD_WINDOW_LOW, DEFAULT_DEMOD_WINDOW_HIGH);
    printf("--demod-no-mark-limits             Normally a symbol in the message data will only be\n");
    printf("                                   considered a 'mark' if it falls between the preamble\n");
    printf("                                   average mark * 0.707 and * 1.414. Setting this option\n");
    printf("                                   will cause a symbol to be considered a 'mark' as long as\n");
    printf("                                   it's greater than it's accompanying 'space'.\n");
    printf("--demod-drop-dup-msgs              Drops duplicate messages that arrive within 5us");
    printf("                                   of each other.\n");
    printf("\n");
}

