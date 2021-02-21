#include "dump1090.h"
#include "demod_multi.h"
#include "demod_2400.h"

static demodulator_context_t *ctx;

#define DEMOD_FN demodulateMulti2000
#define DEMOD_SAMPLES_PER_HALFBIT 1
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti4000
#define DEMOD_SAMPLES_PER_HALFBIT 2
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti6000
#define DEMOD_SAMPLES_PER_HALFBIT 3
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti8000
#define DEMOD_SAMPLES_PER_HALFBIT 4
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti10000
#define DEMOD_SAMPLES_PER_HALFBIT 5
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

#define DEMOD_FN demodulateMulti12000
#define DEMOD_SAMPLES_PER_HALFBIT 6
#include "demod_multi.inc"
#undef DEMOD_FN
#undef DEMOD_SAMPLES_PER_HALFBIT

static demod_fn_t demodulateMultiTask;

/*!
 * @brief Public demodulate function called by the core
 *
 * @param mag mag_buf from core
 */
void demodulateMulti(struct mag_buf *mag)
{
    struct timespec start_time;

    start_cpu_timing(&start_time);
    demodulateMultiTask(mag);
    Modes.stats_current.samples_processed += mag->validLength - mag->overlap;
    Modes.stats_current.samples_dropped += mag->dropped;

    end_cpu_timing(&start_time, &Modes.stats_current.demod_cpu);
    fifo_release(mag);
}

/*!
 * @brief Demodulator initialization
 * @param context (not used)
 */
int demodulateMultiInit(demodulator_context_t *context)
{
    ctx = context;

    if (Modes.sample_rate == 2000000) {
        demodulateMultiTask = demodulateMulti2000;
    }
    else if (Modes.sample_rate == 2400000) {
        demodulateMultiTask = demodulate2400; /* old demod */
    }
    else if (Modes.sample_rate == 4000000) {
        demodulateMultiTask = demodulateMulti4000;
    }
    else if (Modes.sample_rate == 6000000) {
        demodulateMultiTask = demodulateMulti6000;
    }
    else if (Modes.sample_rate == 8000000) {
        demodulateMultiTask = demodulateMulti8000;
    }
    else if (Modes.sample_rate == 10000000) {
        demodulateMultiTask = demodulateMulti10000;
    }
    else if (Modes.sample_rate == 12000000) {
        demodulateMultiTask = demodulateMulti12000;
    }
    else {
        return -1;
    }

    return 0;
}

void demodulateMultiFree(void)
{

}

/*!
 * @brief Print demodulator options help
 */
void demodulateMultiHelp(void)
 {
    return;
}
