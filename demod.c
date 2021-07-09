// Part of dump1090
//
// demod.c: Common demodulator framework
//
// Copyright (c) 2020 George Joseph (g.devel@wxy78.net)
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
#include "demod_2400.h"
#include "demod_hirate.h"
#include <inttypes.h>

typedef struct {
    const char *name;
    const char *description;
    demodulator_type_t demod_type;
    demod_fn_t demod_fn;
    demod_init_fn_t demod_init_fn;
    demod_free_fn_t demod_free_fn;
    demod_show_help_t demod_show_help_fn;
} demodulator_t;

// Thses must be in the same order as demodulator_type_t
static demodulator_t demods[] = {
    { "2400", " Default 2.4 MS/s demodulator", DEMOD_2400, demodulate2400, demodulate2400Init, demodulate2400Free, NULL},
    { "hirate", " HiRate", DEMOD_HIRATE, demodulateHiRate, demodulateHiRateInit, demodulateHiRateFree, demodulateHiRateHelp},
    { NULL, "NULL", DEMOD_NONE, NULL, NULL, NULL, NULL }
};

static demodulator_t *currentDemod;

static demodulator_context_t _ctx = {
    .preamble_threshold_db = DEFAULT_PREAMBLE_THRESHOLD_DB,
    .smoother_window = -1,
    .preamble_strictness = PREAMBLE_STRICTNESS_MAX,
};

static demodulator_context_t *ctx = &_ctx;

static demodulator_t *demodGetByType(demodulator_type_t demod_type)
{
    if (demod_type > DEMOD_NONE) {
        return NULL;
    }
    return &demods[demod_type];
}

static demodulator_t *demodGetByName(const char *name)
{
    int i;

    for (i = 0; i < DEMOD_NONE; i++) {
        if (strcasecmp(name, demods[i].name) == 0) {
            return &demods[i];
        }
    }

    return NULL;
}

int demodInit(void)
{
    if (currentDemod == NULL) {
        currentDemod = demodGetByType(sdrGetDefaultDemodulatorType());
    }

    size_t i;

    ctx->samples_per_symbol = (uint32_t)(Modes.sample_rate / ADSB_SYMBOL_RATE);
    ctx->samples_per_bit = ctx->samples_per_symbol * SYMBOLS_PER_BIT;
    ctx->samples_per_byte = ctx->samples_per_bit * 8;
    ctx->samples_per_preamble = BITS_PER_PREAMBLE * ctx->samples_per_bit;
    ctx->max_samples_per_frame = MODES_LONG_MSG_BYTES * ctx->samples_per_byte + ctx->samples_per_preamble;
    ctx->preamble_threshold = pow(10.0, ctx->preamble_threshold_db / 20.0);

    if (ctx->smoother_window < 0) {
        ctx->smoother_window = ctx->samples_per_symbol;
    }

    if (ctx->preamble_window_low < -((int32_t)ctx->samples_per_symbol)) {
        fprintf(stderr, "Error: preamble_window_low (%d) must be >= -samples_per_symbol(%d)\n",
            ctx->preamble_window_low, -ctx->samples_per_symbol);
        return -1;
    }

    for (i = 0; i < ARRAY_LEN(ctx->sample_symbol_offsets); i++) {
        ctx->sample_symbol_offsets[i] = i * ctx->samples_per_symbol;
    }
    for (i = 0; i < ARRAY_LEN(ctx->sample_bit_offsets); i++) {
        ctx->sample_bit_offsets[i] = i * ctx->samples_per_bit;
    }
    for (i = 0; i < ARRAY_LEN(ctx->sample_byte_offsets); i++) {
        ctx->sample_byte_offsets[i] = i * ctx->samples_per_byte;
    }

    fprintf(stderr, "Demod %s:\n", currentDemod->description);
    fprintf(stderr, "    --demod-smoother-window:  %d samples\n", ctx->smoother_window);
    fprintf(stderr, " --demod-preamble-threshold:  %4.2fdb factor: %4.2f\n", ctx->preamble_threshold_db, ctx->preamble_threshold);
    fprintf(stderr, "--demod-preamble-strictness:  %d\n", ctx->preamble_strictness);
    fprintf(stderr, "    --demod-preamble-window:  %3d samples -> %3d samples  Width: %4d\n", ctx->preamble_window_low, ctx->preamble_window_high, ctx->preamble_window_width);
    fprintf(stderr, "       --demod-demod-window:  %3d samples -> %3d samples  Width: %4d\n", ctx->demod_window_low, ctx->demod_window_high, ctx->demod_window_width);
    fprintf(stderr, "     --demod-no-mark-limits:  %s\n", ctx->no_mark_limits ? "true" : "false");
    fprintf(stderr, "\n");

    if (Modes.stats) {
        Modes.preamble_window_low = ctx->preamble_window_low;
        Modes.preamble_window_high = ctx->preamble_window_high;
        Modes.preamble_window_width = ctx->preamble_window_width;
        Modes.demod_window_low = ctx->demod_window_low;
        Modes.demod_window_high = ctx->demod_window_high;
        Modes.demod_window_width = ctx->demod_window_width;
    }

    if (currentDemod->demod_init_fn) {
        return currentDemod->demod_init_fn(ctx);
    }

    return 0;
}

void demodDemod(struct mag_buf *mag)
{
    currentDemod->demod_fn(mag);
}

#define jsonprintnum(__name, __fmt) (p += sprintf(p, "\"%s\": " __fmt ", ", #__name, ctx->__name))
#define jsonprintstr(__name) (p += sprintf(p, "\"%s\": \"%s\", ", #__name, ctx->__name))

#include "sdr_ifile.h"


void demodFree(void)
{
    if (currentDemod->demod_free_fn) {
        currentDemod->demod_free_fn();
    }
}


bool demodHandleOption(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    int more = j+1 < argc; // There are more arguments

    if (!strcmp(argv[j], "--demod") && more) {
        currentDemod = demodGetByName(argv[++j]);
        if (!currentDemod) {
            fprintf(stderr, "Error: --demod '%s' is unknown.\n", argv[j]);
            fprintf(stderr, "Supported demodulators:\n");
            for (int i = 0; demods[i].name; ++i) {
                fprintf(stderr, "  %s\n", demods[i].name);
            }
            return false;
        }
        *jptr += 1;
    } else if (!strcmp(argv[j], "--demod-smoother-window") && more) {
        ctx->smoother_window = atoi(argv[j + 1]);
        if (ctx->smoother_window <= 0) {
            fprintf(stderr, "Error: %s must be > 0\n", argv[j]);
            return false;
        }
        j++;
    } else if (!strcmp(argv[j], "--demod-preamble-threshold") && more) {
        ctx->preamble_threshold_db = atof(argv[j + 1]);
        if (ctx->preamble_threshold_db < 0.0) {
            fprintf(stderr, "Error: %s must be non-negative\n", argv[j]);
            return false;
        }
        j++;
    } else if (!strcmp(argv[j], "--demod-preamble-strictness") && more) {
        ctx->preamble_strictness = atoi(argv[j + 1]);
        if (ctx->preamble_strictness > PREAMBLE_STRICTNESS_MAX) {
            fprintf(stderr, "Error: %s must be between 0 and 3\n", argv[j]);
            return false;
        }
        j++;
    } else if (!strcmp(argv[j], "--demod-preamble-window") && more) {
        if (sscanf(argv[j + 1], "%d:%d", &ctx->preamble_window_low, &ctx->preamble_window_high) != 2) {
            fprintf(stderr, "Error: %s must be in the format <lowlimit>:<highlimit>\n", argv[j]);
            return false;
        }
        ctx->preamble_window_width = ctx->preamble_window_high - ctx->preamble_window_low + 1;
        if (ctx->preamble_window_width > DEMOD_MAX_WINDOW_WIDTH) {
            fprintf(stderr, "Error: %s Window size %d exceeds max width of %d\n", argv[j],
                ctx->preamble_window_high, DEMOD_MAX_WINDOW_WIDTH);
            return false;
        }
        j++;
    } else if (!strcmp(argv[j], "--demod-msg-window") && more) {
        if (sscanf(argv[j + 1], "%d:%d", &ctx->demod_window_low, &ctx->demod_window_high) != 2) {
            fprintf(stderr, "Error: %s must be in the format <lowlimit>:<highlimit>\n", argv[j]);
            return false;
        }
        ctx->demod_window_width = ctx->demod_window_high - ctx->demod_window_low + 1;
        if (ctx->demod_window_width > DEMOD_MAX_WINDOW_WIDTH) {
            fprintf(stderr, "Error: %s Window size %d exceeds max width of %d\n", argv[j],
                ctx->demod_window_high - ctx->demod_window_low + 1, DEMOD_MAX_WINDOW_WIDTH);
            return false;
        }
        j++;
    } else if (!strcmp(argv[j], "--demod-no-mark-limits")) {
        ctx->no_mark_limits = true;
    } else {
        return false;
    }

    *jptr = j;

    return true;
}

void demodShowHelp(void)
{
    int i;

    printf(
"      Demodulator specfic options\n\n"
"--demod <demod>          Set demodulator.  One of:\n");

    for( i = 0; i < DEMOD_NONE; i++) {
        printf(
"                         %s: %s\n", demods[i].name, demods[i].description);
    }
    printf("\n");

    printf("      Common Demodulator options\n");

    printf("\n");
    printf("--demod-smoother-window <n>        Smoother window size in samples (default: samples/symbol)\n");
    printf("--demod-preamble-threshold <n>     Preamble threshold in dB (default: 3dB)\n");
    printf("--demod-premble-window <l>:<h>     l: Offset from the expected start of a preamble to start the\n");
    printf("                                   search for an actual preamble.  May be negative.\n");
    printf("                                   h: Offset from the expected start of a preamble to stop the\n");
    printf("                                   search for an actual preamble.\n");
    printf("                                   The default, and the max, is <-samples/symbol>:<samples/symbol>\n");
    printf("--demod-preamble-strictness <n>    Determines how strict preamble detection should be.\n");
    printf("                                   0=least strict, 3=most strict.  The default is 3.\n");
    printf("\n");


    for (i = 0; demods[i].name; i++) {
        if (demods[i].demod_show_help_fn) {
            demods[i].demod_show_help_fn();
        }
    }
}

int32_t demodCheckPreamble(const uint16_t *sa, const uint16_t *sc, demodulator_context_t *ctx,
    uint16_t *preamble_avg_mark, uint16_t *preamble_avg_space, uint32_t *preamble_best_offset )
{

    /* verify pulse shapes */
    if (! (sa[Q1A] < sa[P1] &&
           sa[Q1B] < sa[P1] &&
           sa[Q2A] < sa[P2] &&
           sa[Q2B] < sa[P2] &&
           sa[Q3A] < sa[P3] &&
           sa[Q3B] < sa[P3] &&
           sa[Q4A] < sa[P4] &&
           sa[Q4B] < sa[P4]) )
        return -1;

    /* find nearby correlation peak */
    unsigned best = P1, best_corr = 0;
    for (unsigned k = P1 + ctx->preamble_window_low; k <= P1 + ctx->preamble_window_high; ++k) {
        if (sc[k] > best_corr) {
            best = k;
            best_corr = sc[k];
        }
    }

    /* check at halfbit offset, should be substantially worse correlation */
    if ((ctx->preamble_strictness & PREAMBLE_STRICTNESS_HALFBIT)
        && sc[best + ctx->samples_per_symbol] * 2 > sc[best])
        return -2;

    /* exclude false positives where we have a strong preamble with
     * pulses 1/2 arriving 7 halfbits after our P1 correlation, i.e.:
     *
     * preamble correlator function:
     *      __    __             __    __               X  bit  X
     *   __|  |__|  |___________|  |__|  |______________X   0   X
     *   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
     *   Q1A   Q1B            Q3A  Q3B      Q5C         | ---->
     *      P1    P2    Q5A      P3    P4      Q5D      | message
     *         Q2A  Q2B    Q5B     Q4A   Q4B            | data
     *
     * incoming signal magnitude:
     *
     *   noise fools the         __    __  <-strong-> __    __
     *   pulse shape check ..   |  |  |  |   signal  |  |  |  |
     *   ___xxx___xxx___________|  |__|  |___________|  |__|  |__
     *   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
     *
     * -> weak correlation for a preamble starting at halfbit 1,
     *    strong correlation for a preamble starting at halfbit 8
     */
    if ((ctx->preamble_strictness & PREAMBLE_STRICTNESS_HALFBIT)
        && sc[best + ctx->samples_per_symbol * 7] > sc[best] * 2)
        return -3;

    if (preamble_best_offset) {
        *preamble_best_offset = best;
    }
    int16_t offset = best - ctx->samples_per_symbol;
    if (preamble_avg_mark) {
        *preamble_avg_mark = ((sa[offset + P1] + sa[offset + P2] + sa[offset + P3] + sa[offset + P4]) / 4);
    }
    if (preamble_avg_space) {
        *preamble_avg_space = ((sa[offset + Q5A] + sa[offset + Q5B] + sa[offset + Q5C] + sa[offset + Q5D]) / 4);
    }

    if (Modes.stats) {
        Modes.stats_current.demod_preamble_distro[DISTRO_OFFSET(offset)]++;
    }

    return 0;
}

void processSignalAndNoise(struct modesMessage *mm, uint16_t preamble_avg_mark, uint16_t preamble_avg_space)
{
    /* Scale the mean level (0 -> 65535) to 0.0 -> 1.0 */
    float signal_Vpk = (preamble_avg_mark / MAX_AMPLITUDE);
    /* mm.signalLevel is actually the power, not level */
    mm->signalLevel = (signal_Vpk * signal_Vpk);
    Modes.stats_current.signal_power_sum += mm->signalLevel;
    Modes.stats_current.signal_power_count += 1;

    if (mm->signalLevel > Modes.stats_current.peak_signal_power) {

        Modes.stats_current.peak_signal_power = mm->signalLevel;
    }

    /* If the signal power is above -3dBFS increment the "strong signal" counter. */
    if (mm->signalLevel > 0.50119) {
        Modes.stats_current.strong_signal_count++;
    }

    /*
     * Now update the noise stats from the power that was calculated
     * during conversion.
     */
    /* Scale the mean level (0 -> 65535) to 0.0 -> 1.0 */
    float noise_Vpk = (preamble_avg_space / MAX_AMPLITUDE);
    mm->noiseLevel = (noise_Vpk * noise_Vpk);
    Modes.stats_current.noise_power_sum += mm->noiseLevel;
    Modes.stats_current.noise_power_count += 1;
}
