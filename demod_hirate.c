// Part of dump1090, a Mode S message decoder
//
// demod_hirate.h: Hi Rate Mode S demodulator
//
// Copyright (c) 2020 George Joseph <g.devel@wxy78.net>
//
// Function running_sum suggested by Oliver Jowitt
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
#include <inttypes.h>
#include <stdlib.h>

#define SYMBOLS_PER_BIT 2
#define BITS_PER_PREAMBLE 8
#define SYMBOLS_PER_PREAMBLE (BITS_PER_PREAMBLE * 2)
#define ADSB_DATA_RATE 1000000
#define ADSB_SYMBOL_RATE (ADSB_DATA_RATE * SYMBOLS_PER_BIT)

static uint32_t samples_per_symbol;
static uint32_t samples_per_bit;
static uint32_t samples_per_byte;
static uint32_t samples_per_preamble;
static uint32_t max_samples_per_frame;

#define MSG_MAX_DECODE_TRIES 16

#define PERFECT_PREAMBLE_SCORE    20
#define DEFAULT_HIRATE_THRESHOLD 0.4
#define DEFAULT_HIRATE_SCORE      19
#define DEFAULT_HIRATE_WINDOW      3
#define DEFAULT_HIRATE_TRIES      11

#define ALLOW_HIRATE_THRESHOLD
#define ALLOW_HIRATE_SCORE
#define ALLOW_HIRATE_WINDOW
#define ALLOW_HIRATE_TRIES

#if (defined(ALLOW_HIRATE_THRESHOLD) || defined(ALLOW_HIRATE_SCORE) || defined(ALLOW_HIRATE_WINDOW) || defined(ALLOW_HIRATE_TRIES))
#define ALLOW_OPTIONS
#endif

#ifdef ALLOW_HIRATE_THRESHOLD
static float threshold_factor = DEFAULT_HIRATE_THRESHOLD;
#else
#define threshold_factor DEFAULT_HIRATE_THRESHOLD
#endif

#ifdef ALLOW_HIRATE_SCORE
static int32_t preamble_score = DEFAULT_HIRATE_SCORE;
#else
#define preamble_score DEFAULT_HIRATE_SCORE
#endif

#ifdef ALLOW_HIRATE_WINDOW
static uint32_t running_sum_window = DEFAULT_HIRATE_WINDOW;
#else
#define running_sum_window DEFAULT_HIRATE_WINDOW
#endif

#ifdef ALLOW_HIRATE_TRIES
static uint32_t msg_decode_tries = DEFAULT_HIRATE_TRIES;
#else
#define msg_decode_tries DEFAULT_HIRATE_TRIES
#endif


static uint64_t decode_distro[MSG_MAX_DECODE_TRIES] = {0, };

struct message_context {
    uint8_t buffer[MSG_MAX_DECODE_TRIES][MODES_LONG_MSG_BYTES];
    uint32_t current_message_index;
    uint8_t *best_message;
    uint32_t best_message_index;
    uint32_t best_message_start;
    uint32_t msg_bytelen[MSG_MAX_DECODE_TRIES];
    uint8_t df[MSG_MAX_DECODE_TRIES];
    int32_t score[MSG_MAX_DECODE_TRIES];
    uint16_t *pristine_buffer;
    uint16_t threshold;
    uint32_t messages_scored;
    uint64_t sum_mark_signal_level[MSG_MAX_DECODE_TRIES];
};

/*
 * Both gcc and clang do a good job on their own of inlining
 * the following short functions.  No need to make them macros
 * or provide hints to the compiler.  In fact, the more hints
 * you give to the compiler, the worse performance seems to get.
 */

/*!
 * @brief Reaad 1 bit from the buffer
 *
 * @param buf input buffer
 * @param next position in buffer to read
 * @param msg message context
 * @return bit value
 *
 * This function assumes that running_sum() has already been run on the buffer
 * and therefore compares only the first samples from 2 successive symbols.
 */
static bool readBit(const uint32_t *buf, const uint32_t next, struct message_context *msg)
{
    uint16_t ba = 0, bb = 0;
    ba = buf[next];
    bb = buf[next + samples_per_symbol];

    /*
     * We need to accumulate the signal levels from the pristine buffer
     * for the "mark" symbols to properly calculate the mean signal
     * level for the message.  If we tried to use the levels from
     * the buffer that was modified by running_sum(), we'd get bad
     * levels.
     */
    if (ba > bb) {
        msg->sum_mark_signal_level[msg->current_message_index]
            += msg->pristine_buffer[next + msg->current_message_index];
        return 1;
    } else {
        msg->sum_mark_signal_level[msg->current_message_index]
            += msg->pristine_buffer[next + msg->current_message_index + samples_per_symbol];
        return 0;
    }
}

/*!
 * @brief Reaad 1 byte from the buffer
 *
 * @param buf input buffer
 * @param next position in buffer to read
 * @param msg message context
 * @return byte read
 *
 */
static uint8_t readByte(const uint32_t *buf, const uint32_t next, struct message_context *msg)
{
    int8_t bit;
    int32_t i;
    uint8_t byte = 0;
    uint32_t n = next;

    for (i=7 ; i >= 0 ; i--) {
        bit = readBit(buf, n, msg);
        n += samples_per_bit;
        byte |= (bit << i);
    }
    return byte;
}

/*!
 * @brief Read an arbitrary number of bytes from the buffer
 *
 * @param buf input buffer
 * @param next position in buffer to read
 * @param outbuf pointer to the output buffer
 * @param count number of bytes to read
 * @param msg message context
 */
static void readBytes(const uint32_t *buf, const uint32_t next, uint8_t *outbuf,
    const uint32_t count, struct message_context *msg)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        outbuf[i] = readByte(buf, next + (samples_per_byte * i), msg);
    }
}

/*
 * The preamble symbols:
 *
 * 0123456789012345
 * 1010000101000000
 * /\/\___/\/\_____
 * ^_^____^_^______
 */

/*!
 * @brief Check if the buffer provided starts with a oreamble
 * @param buf input buffer
 * @return score (20 is perfect match)
 */
static int32_t check_preamble(const uint32_t *buf)
{
    uint32_t i;
    int32_t score = 0;
    uint16_t symbols[SYMBOLS_PER_PREAMBLE];

    for (i = 0; i < SYMBOLS_PER_PREAMBLE; i++) {
        symbols[i] = buf[samples_per_symbol * i];
    }

    /*
     * Max score is 20.
     * The transition symbols (mark -> space or space -> mark) carry more weight than
     * the all-space symbols.
     */

    score += (symbols[1] < symbols[0]) * 2;
    score += (symbols[2] > symbols[1]) * 2;
    score += (symbols[3] > symbols[2]);
    score += (symbols[4] < symbols[2]);
    score += (symbols[5] < symbols[2]);
    score += (symbols[6] < symbols[7]) * 2;
    score += (symbols[7] > symbols[8]) * 2;
    score += (symbols[8] < symbols[9]) * 2;
    score += (symbols[9] > symbols[10]);
    score += (symbols[10] < symbols[9]);
    score += (symbols[11] < symbols[9]);
    score += (symbols[12] < symbols[9]);
    score += (symbols[13] < symbols[9]);
    score += (symbols[14] < symbols[9]);
    score += (symbols[15] < symbols[9]);

    return score;
}

/*!
 * @brief Check the provided DF code to see if it's valid
 *
 * @param df df code
 * @param msg messasge context
 * @return boolean indicating if a valid DF code was found
 *
 * This function also sets the message context's byte length
 * and DF code.
 */
static bool check_df(const uint8_t df, struct message_context *msg)
{
    switch (df) {
    case 0: case 4: case 5: case 11:
        msg->msg_bytelen[msg->current_message_index] = MODES_SHORT_MSG_BYTES;
        msg->df[msg->current_message_index] = df;
        return 1;
    case 16: case 17: case 18: case 20: case 21: case 24:
        msg->msg_bytelen[msg->current_message_index] = MODES_LONG_MSG_BYTES;
        msg->df[msg->current_message_index] = df;
        return 1;
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
static int32_t get_message(const uint32_t *buf, uint32_t next, struct message_context *msg)
{
    uint32_t i;
    int32_t best_score = -100;
    uint8_t df_byte;

    /*
     * Score "msg_decode_tries" times, incrementing the buffer pointer each time.
     * When we're done, keep the one with the best score (if there is one).
     *
     * Stopping after we find the first "good" score can decrease the number of
     * accepted messages by a good 5% whereas continuing and keeping the best
     * costs almost nothing (provided the "tries" count is reasonable).
     */
    for (i = 0; i < ((const uint32_t)msg_decode_tries); i++) {
        msg->current_message_index = i;
        /*
         * Fast Fail.  If we don't have a valid DF, don't bother reading the rest of the message.
         */
        df_byte = readByte(buf, next + i, msg);
        if (!check_df(df_byte >> 3, msg)) {
            msg->sum_mark_signal_level[i] = 0;
            continue;
        }
        msg->messages_scored++;
        msg->buffer[i][0] = df_byte;
        readBytes(buf, next + i + samples_per_byte, &msg->buffer[i][1], msg->msg_bytelen[i] - 1, msg);
        msg->score[i] = scoreModesMessage(msg->buffer[i], msg->msg_bytelen[i] << 3);
        if (msg->score[i] > best_score) {
            best_score = msg->score[i];
            msg->best_message = msg->buffer[i];
            msg->best_message_start = next + i;
            msg->best_message_index = i;
        }
    }

    /*
     * Keep track of the successful offsets to aid in tuning msg_decode_tries.
     */
    if (Modes.stats && best_score > 0) {
        decode_distro[msg->best_message_index]++;
    }

    return best_score;
}

/*!
 * @brief Creates a running sum if teh inpout buffer
 *
 * @param in unaltered input buffer
 * @param out summed output with threshold applied
 * @param len samples to process
 * @param threshold threshold value to apply
 */
static void running_sum(uint16_t *in, uint32_t *out, unsigned len, const uint16_t threshold)
{
    uint32_t i;
    uint32_t j;
    uint16_t s;

    for (i = 0; i < len; i++) {
        out[i] = 0;
        for (j = 0; j < running_sum_window; j++) {
            /*
             * The whole process seems to work better if we reduce
             * the magnitude by half before applying the threshold
             * and adding the result.
             */
            s = (in[i + j] >> 1 );
            s = s < threshold ? 0 : s - threshold;
            out[i]+=s;
        }
    }
}

/*!
 * @brief Main demodulator task
 *
 * @param data mag_buf from core
 */
static void demodulateHiRateTask(void *data)
{
    struct mag_buf *mag = data;
    uint32_t *uin;
    uint32_t *samplebuf;
    uint32_t mlen = mag->validLength - mag->overlap;
    uint32_t next_sample = 0;
    uint32_t message_start;
    uint32_t message_start_offset;
    struct message_context msg_buffer;
    uint8_t *best_msg;
    int32_t score;
    uint32_t msg_bitlen;
    uint32_t msg_samplelen;
    static struct modesMessage zeroMessage;
    struct modesMessage mm;
    uint16_t threshold = ((uint16_t)(mag->mean_level * MAX_AMPLITUDE)) * threshold_factor;
    uint64_t sum_signal_level;
    uint64_t mean_signal_level;
    double signal_Vpk;

    /*
     * We need to keep the original buffer to calculate signal strength
     * so we need to allocate a new one to hold the results of the
     * running sum.
     */
    uin = malloc(sizeof(*uin) * mag->validLength);
    running_sum(mag->data, uin, mlen, threshold);

    while (next_sample < mlen) {
        /*
         * Fast Fail.  A preamble can't start with samples below the threshold
         * so we just skip over them.
         */
        if (uin[next_sample] < threshold) {
            next_sample += 1;
            continue;
        }

        /*
         * samplebuf marks the start of this test.
         */
        samplebuf = uin + next_sample;

        /*
         * Is there a valid preamble?
         */
        score = check_preamble(samplebuf);
        if (score < preamble_score) {
            next_sample += 1;
            continue;
        }

        Modes.stats_current.demod_preambles++;
        /*
         * Initially, message_start is set to the calculated location
         * in the buffer.
         */
        message_start = samples_per_preamble;
        memset(&msg_buffer, 0, sizeof(msg_buffer));
        msg_buffer.threshold = threshold;

        /*
         * The search for valid messages actually starts _before_ the calculated
         * start position.  We back up by half the number of tries so the calculated
         * position is in the middle of the search range.
         */
        message_start_offset = (msg_decode_tries / 2);
        /*
         * The pristine_buffer is an index into the unmodified input buffer that
         * corresponds to the start of the samplebuffer.  We need this to calculate
         * the correct signal level.
         */
        msg_buffer.pristine_buffer = mag->data + next_sample - message_start_offset;

        score = get_message(samplebuf, message_start - message_start_offset, &msg_buffer);
        best_msg = msg_buffer.best_message;

        if (score < 0) {
            if (score == -1) {
                Modes.stats_current.demod_rejected_unknown_icao++;
            } else {
                Modes.stats_current.demod_rejected_bad++;
            }
            next_sample += 1;
            continue;
        }
        msg_bitlen = msg_buffer.msg_bytelen[msg_buffer.best_message_index] << 3;
        msg_samplelen = msg_bitlen * samples_per_bit;

        /*
         * We no have to adjust message_start to point to the start of
         * the best scored message which is the one we're going to use.
         */
        message_start = msg_buffer.best_message_start;

        // Set initial mm structure details
        mm = zeroMessage;

        /*
         * For consistency with how the Beast / Radarcape does it,
         * we report the timestamp at the end of bit 56 (even if
         * the frame is a 112-bit frame)
         */
        mm.timestampMsg = mag->sampleTimestamp +
            //  preamble start offset in 12MHz clock ticks from input buffer start
            ((next_sample * 12) / samples_per_bit) +
            //  message-end offset from preamble start in 12MHz clock ticks
            ((8 + 56) * 12);

        // compute message receive time as block-start-time + difference in the 12MHz clock
        mm.sysTimestampMsg = mag->sysTimestamp + receiveclock_ms_elapsed(mag->sampleTimestamp, mm.timestampMsg);

        mm.score = score;

        score = decodeModesMessage(&mm, best_msg);
        if (score < 0) {
            if (score == -1) {
                Modes.stats_current.demod_rejected_unknown_icao++;
            } else {
                Modes.stats_current.demod_rejected_bad++;
            }
            next_sample += 1;
            continue;
        }

        Modes.stats_current.demod_accepted[mm.correctedbits]++;

        /*
         * As we were reading the message, we were accumulating the raw signal levels
         * from the input buffer for all of the "mark" symbols.  Since there is ALWAYS
         * one "mark" symbol per bit in the message, we can divide the accumulated
         * level by the number of bits in the message to get the mean singal level.
         */
        sum_signal_level = msg_buffer.sum_mark_signal_level[msg_buffer.best_message_index];
        mean_signal_level = sum_signal_level / msg_bitlen;

        /* Scale the mean level (0 -> 65535) to 0.0 -> 1.0 */
        signal_Vpk = (mean_signal_level / MAX_AMPLITUDE);
        /* mm.signalLevel is actually the power, not level */
        mm.signalLevel = (signal_Vpk * signal_Vpk);
        Modes.stats_current.signal_power_sum += mm.signalLevel;
        Modes.stats_current.signal_power_count += 1;

        if (mm.signalLevel > Modes.stats_current.peak_signal_power) {

            Modes.stats_current.peak_signal_power = mm.signalLevel;
        }

        /* If the signal power is above -3dBFS increment the "strong signal" counter. */
        if (mm.signalLevel > 0.50119) {
            Modes.stats_current.strong_signal_count++;
        }

        /*
         * Now update the noise stats from the power that was calculated
         * during conversion.
         */
        Modes.stats_current.noise_power_sum += mag->mean_power;
        Modes.stats_current.noise_power_count += 1;

        /* That's it.  Use the message. */
        useModesMessage(&mm);

        /*
         * From demod_2400 comments...
         * Skip over the message:
         * (we actually skip to 8 bits before the end of the message,
         * because we can often decode two messages that *almost* collide,
         * where the preamble of the second message clobbered the last
         * few bits of the first message, but the message bits didn't
         * overlap)
         */
        next_sample += (msg_samplelen - samples_per_preamble);
    }

    free(uin);
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
void demodulateHiRateInit(void *context)
{
    MODES_NOTUSED(context);

    samples_per_symbol = (uint32_t)(Modes.sample_rate / ADSB_SYMBOL_RATE);
    samples_per_bit = samples_per_symbol * SYMBOLS_PER_BIT;
    samples_per_byte = samples_per_bit * 8;
    samples_per_preamble = BITS_PER_PREAMBLE * samples_per_bit;
    max_samples_per_frame = MODES_LONG_MSG_BYTES * samples_per_byte + samples_per_preamble;

#ifdef ALLOW_OPTIONS
    fprintf(stderr, "Demod HiRate:\n");
#ifdef ALLOW_HIRATE_THRESHOLD
    fprintf(stderr, "  hirate-threshold:       %4.1f\n", threshold_factor);
#endif
#ifdef ALLOW_HIRATE_SCORE
    fprintf(stderr, "     hirate-score:        %4d\n", preamble_score);
#endif
#ifdef ALLOW_HIRATE_WINDOW
    fprintf(stderr, "     hirate-window:       %4d\n", running_sum_window);
#endif
#ifdef ALLOW_HIRATE_TRIES
    fprintf(stderr, "      hirate-tries:       %4d\n", msg_decode_tries);
#endif
    fprintf(stderr, "\n");
#endif
}

/*!
 * Demodulator cleanup
 * @param context (not used)
 */
void demodulateHiRateFree(void *context)
{
    MODES_NOTUSED(context);

    if (Modes.stats) {
        uint32_t i;
        int maxlen = 0;;
        char buf[65];

        for (i = 0; i < msg_decode_tries; i++) {
            int len = sprintf(buf, "%"PRIu64"", decode_distro[i]);
            if (len > maxlen) {
                maxlen = len;
            }
        }

        printf("\n");
        printf("Demod Hirate:\n");
        printf("Successful Message Decode Offsets\n");
        printf("Decode Tries: %d\n", msg_decode_tries);
            printf("Offset   %*s\n", maxlen, "Count");

        for (i = 0; i < msg_decode_tries; i++) {
            printf("%3d:    %*" PRIu64 "\n", i -( msg_decode_tries / 2), maxlen + 2, decode_distro[i]);
        }
        printf("\n");
    }

}

#ifdef ALLOW_OPTIONS
/*!
 * @brief Print demodulator options help
 */
void demodulateHiRateHelp(void)
{
    printf("      HiRate Demodulator specific options (use with --demod hirate)\n");

    printf("\n");
#ifdef ALLOW_HIRATE_THRESHOLD
    printf("--hirate-threshold <threshold>      A multiplication factor applied to the\n");
    printf("                                    mean buffer signal strength.  Samples\n");
    printf("                                    below the adjusted value will be forced\n");
    printf("                                    to 0.  The default is %3.1f\n", DEFAULT_HIRATE_THRESHOLD);
#endif
#ifdef ALLOW_HIRATE_SCORE
    printf("--hirate-score <preamble score>     The passing score for a successful preamble\n");
    printf("                                    test.  Perfect score is %d.  Default is %d.\n",
        PERFECT_PREAMBLE_SCORE, PERFECT_PREAMBLE_SCORE - 1);
#endif
#ifdef ALLOW_HIRATE_WINDOW
    printf("--hirate-window <samples>           How many samples are summed together to\n");
    printf("                                    determine the value of a symbol.\n");
    printf("                                    Must > 1.  The default is %d.\n", DEFAULT_HIRATE_WINDOW);
#endif
#ifdef ALLOW_HIRATE_TRIES
    printf("--hirate-tries <tries>              The number of attempts made to decode the\n");
    printf("                                    same set of samples.\n");
    printf("                                    Must be between 1 and %d.\n", MSG_MAX_DECODE_TRIES - 1);
    printf("                                    The default is %d\n", DEFAULT_HIRATE_TRIES);
#endif
    printf("\n");
}
#else
void demodulateHiRateHelp(void)
{

}
#endif

#ifdef ALLOW_OPTIONS
/*!
 * @brief Process demodulator options
 *
 * @param argc
 * @param argv
 * @param jptr
 * @return boolean indicating whether option was handled or not
 */
bool demodulateHiRateOptions(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    bool more = (j+1 < argc);
    if (0) {
#ifdef ALLOW_HIRATE_THRESHOLD
    } else if (!strcmp(argv[j], "--hirate-threshold") && more) {
        threshold_factor = atof(argv[j + 1]);
        if (threshold_factor < 0.0) {
            fprintf(stderr, "Error: %s must be non-negative\n", argv[j]);
            return false;
        }
        j++;
#endif
#ifdef ALLOW_HIRATE_SCORE
    } else if (!strcmp(argv[j], "--hirate-score") && more) {
        preamble_score = atoi(argv[j + 1]);
        if (preamble_score < 0 || preamble_score > PERFECT_PREAMBLE_SCORE) {
            fprintf(stderr, "Error: %s must be between 0 and %d\n", argv[j], PERFECT_PREAMBLE_SCORE);
            return false;
        }
        j++;
#endif
#ifdef ALLOW_HIRATE_WINDOW
    } else if (!strcmp(argv[j], "--hirate-window") && more) {
        running_sum_window = atoi(argv[j + 1]);
        if (running_sum_window <= 0) {
            fprintf(stderr, "Error: %s must be > 0\n", argv[j]);
            return false;
        }
        j++;
#endif
#ifdef ALLOW_HIRATE_TRIES
    } else if (!strcmp(argv[j], "--hirate-tries") && more) {
        msg_decode_tries = atoi(argv[j + 1]);
        if (msg_decode_tries >= MSG_MAX_DECODE_TRIES) {
            fprintf(stderr, "Error: %s must be between 1 and %d\n", argv[j], MSG_MAX_DECODE_TRIES);
            return false;
        }
        j++;
#endif
    } else {
        return false;
    }

    *jptr = j;

    return true;
}
#else
bool demodulateHiRateOptions(int argc, char **argv, int *jptr)
{
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);
    MODES_NOTUSED(jptr);

    return false;
}
#endif
