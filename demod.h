// Part of dump1090
//
// demod.h: Common demodulator framework
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

#ifndef DEMOD_H
#define DEMOD_H

struct mag_buf;

#define SYMBOLS_PER_BIT     2
#define BITS_PER_PREAMBLE   8
#define ADSB_DATA_RATE      1e6
#define ADSB_SYMBOL_RATE (ADSB_DATA_RATE * SYMBOLS_PER_BIT)
#define SYMBOLS_PER_PREAMBLE (BITS_PER_PREAMBLE * 2)

#define DEFAULT_PREAMBLE_THRESHOLD_DB 3.0
#define MAX_WINDOW_WIDTH               15
#define WINDOW_MIDPOINT                 7
#define DEFAULT_DEMOD_WINDOW_WIDTH      1
#define DEFAULT_DEMOD_WINDOW_LOW        0
#define DEFAULT_DEMOD_WINDOW_HIGH       0
#define DEFAULT_PREAMBLE_WINDOW_WIDTH   1
#define DEFAULT_PREAMBLE_WINDOW_LOW     0
#define DEFAULT_PREAMBLE_WINDOW_HIGH    0

#define DISTRO_OFFSET(i) (WINDOW_MIDPOINT + i)

typedef enum {
    PREAMBLE_STRICTNESS_NONE = 0,
    PREAMBLE_STRICTNESS_HALFBIT = 0x1,
    PREAMBLE_STRICTNESS_STRONG = 0x2,
    PREAMBLE_STRICTNESS_MAX = 0x3,
} preamble_strictness_t;

typedef struct {
    uint32_t    samples_per_symbol;
    uint32_t    samples_per_bit;
    uint32_t    samples_per_byte;
    uint32_t    samples_per_preamble;
    uint32_t    max_samples_per_frame;
    uint32_t    sample_symbol_offsets[16];
    uint32_t    sample_bit_offsets[8];
    uint32_t    sample_byte_offsets[15];
    double      preamble_threshold_db;
    double      preamble_threshold;
    int32_t    smoother_window;
    uint32_t    demod_window_width;
    int32_t     demod_window_low;
    int32_t     demod_window_high;
    uint32_t    preamble_window_width;
    int32_t     preamble_window_low;
    int32_t     preamble_window_high;
    bool        no_mark_limits;
    preamble_strictness_t preamble_strictness;
    uint64_t    decode_distro[MAX_WINDOW_WIDTH];
    uint64_t    preamble_distro[MAX_WINDOW_WIDTH];
} demodulator_context_t;

typedef void (* demod_fn_t)(struct mag_buf *mag);
typedef int (* demod_init_fn_t)(demodulator_context_t *ctx);
typedef void (* demod_free_fn_t)(void);
typedef void (* demod_show_help_t)(void);

typedef enum { DEMOD_2400 = 0, DEMOD_HIRATE, DEMOD_MULTI, DEMOD_NONE } demodulator_type_t;

bool demodHandleOption(int argc, char **argv, int *jptr);
int demodInit(void);
void demodDemod(struct mag_buf *mag);
void demodFree(void);
void demodShowHelp(void);

/* exclude false positives where we have a strong preamble with
 * pulses 1/2 arriving 7 halfbits after our P1 correlation, i.e.:
 *
 * preamble correlator function:
 *
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

/* Preamble pulse offsets, in samples */
#define P1 ctx->sample_symbol_offsets[1]
#define P2 ctx->sample_symbol_offsets[3]
#define P3 ctx->sample_symbol_offsets[8]
#define P4 ctx->sample_symbol_offsets[10]
/* Quiet period offsets, in samples */
#define Q1A ctx->sample_symbol_offsets[0]
#define Q1B ctx->sample_symbol_offsets[2]
#define Q2A ctx->sample_symbol_offsets[2]
#define Q2B ctx->sample_symbol_offsets[4]
#define Q3A ctx->sample_symbol_offsets[7]
#define Q3B ctx->sample_symbol_offsets[9]
#define Q4A ctx->sample_symbol_offsets[9]
#define Q4B ctx->sample_symbol_offsets[11]
#define Q5A ctx->sample_symbol_offsets[5]
#define Q5B ctx->sample_symbol_offsets[6]
#define Q5C ctx->sample_symbol_offsets[12]
#define Q5D ctx->sample_symbol_offsets[13]


extern uint32_t sample_symbol_offsets[16];

int32_t demodCheckPreamble(const uint16_t *sa, const uint16_t *sc, demodulator_context_t *ctx,
    uint16_t *preamble_avg_mark, uint16_t *preamble_avg_space, uint32_t *preamble_best_offset );

void processSignalAndNoise(struct modesMessage *mm, uint16_t preamble_avg_mark, uint16_t preamble_avg_space);

char *generateDemodJson(const char *url_path, int *len);

#endif
