// Part of dump1090, a Mode S message intrface for AirSpy.
//
// sdr_airspy.c: AirSpy support
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
#include "sdr_airspy.h"

#include <libairspy/airspy.h>
#include <libairspy/filters.h>
#include <inttypes.h>

static struct {
    struct airspy_device *device;
    uint64_t serial;
    int lna_gain;
    int mixer_gain;
    int vga_gain;
    int linearity_gain;
    int sensitivity_gain;
    int individual_gains_set;
    int preset_gains_set;
    int lna_agc;
    int mixer_agc;
    int agcs_set;
    int rf_bias;
    int packing;

    iq_convert_fn converter;
    struct converter_state *converter_state;
} AirSpy;

static int bytes_per_sample = 0;

void airspyInitConfig()
{
    AirSpy.device = NULL;
    AirSpy.serial = 0;
    AirSpy.lna_gain = -1;
    AirSpy.mixer_gain = -1;
    AirSpy.vga_gain = -1;
    AirSpy.linearity_gain = -1;
    AirSpy.sensitivity_gain = -1;
    AirSpy.individual_gains_set = 0;
    AirSpy.preset_gains_set = 0;
    AirSpy.lna_agc = 0;
    AirSpy.mixer_agc = 0;
    AirSpy.agcs_set = 0;
    AirSpy.rf_bias = 0;
    AirSpy.converter = NULL;
    AirSpy.converter_state = NULL;
}

bool airspyHandleOption(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    bool more = (j+1 < argc);
    if (!strcmp(argv[j], "--airspy-lna-gain") && more) {
        AirSpy.lna_gain = atoi(argv[++j]);

        if (AirSpy.lna_gain > 15 || AirSpy.lna_gain < 0) {
            fprintf(stderr, "Error: --airspy-lna-gain range is 0 - 15\n");
            return false;
        }
        AirSpy.individual_gains_set++;
    } else if (!strcmp(argv[j], "--airspy-vga-gain") && more) {
        AirSpy.vga_gain = atoi(argv[++j]);

        if (AirSpy.vga_gain > 15 || AirSpy.vga_gain < 0) {
            fprintf(stderr, "Error: --airspy-vga-gain range is 0 - 15\n");
            return false;
        }
        AirSpy.individual_gains_set++;
    } else if (!strcmp(argv[j], "--airspy-mixer-gain") && more) {
        AirSpy.mixer_gain = atoi(argv[++j]);

        if (AirSpy.mixer_gain > 15 || AirSpy.mixer_gain < 0) {
            fprintf(stderr, "Error: --airspy-mixer-gain range is 0 - 15\n");
            return false;
        }
        AirSpy.individual_gains_set++;
    } else if (!strcmp(argv[j], "--airspy-linearity-gain") && more) {
        AirSpy.linearity_gain = atoi(argv[++j]);

        if (AirSpy.linearity_gain > 21 || AirSpy.linearity_gain < 0) {
            fprintf(stderr, "Error: --airspy-linearity-gain range is 0 - 21\n");
            return false;
        }
        AirSpy.preset_gains_set++;
    } else if (!strcmp(argv[j], "--airspy-sensitivity-gain") && more) {
        AirSpy.sensitivity_gain = atoi(argv[++j]);

        if (AirSpy.sensitivity_gain > 21 || AirSpy.sensitivity_gain < 0) {
            fprintf(stderr, "Error: --airspy-sensitivity-gain range is 0 - 21\n");
            return false;
        }
        AirSpy.preset_gains_set++;
    } else if (!strcmp(argv[j], "--airspy-enable-lna-agc")) {
        AirSpy.lna_agc = 1;
        AirSpy.agcs_set++;
    } else if (!strcmp(argv[j], "--airspy-enable-mixer-agc")) {
        AirSpy.mixer_agc = 1;
        AirSpy.agcs_set++;
    } else if (!strcmp(argv[j], "--airspy-enable-packing")) {
        AirSpy.packing = 1;
    } else if (!strcmp(argv[j], "--airspy-enable-rf-bias")) {
        AirSpy.rf_bias = 1;
    } else {
        return false;
    }

    *jptr = j;

    return true;
}

void airspyShowHelp()
{

    printf("      AirSpy-specific options (use with --device-type airspy)\n");

    printf("\n");
    printf("--device <serial>                select device by hex serial number\n");
    printf("--airspy-lna-gain <gain>         set lna gain (Range 0-15)\n");
    printf("--airspy-mixer-gain <gain>       set mixer gain (Range 0-15)\n");
    printf("--airspy-vga-gain <gain>         set vga gain (Range 0-15)\n");
    printf("--airspy-linearity-gain <gain>   set linearity gain presets (Range 0-21) (default 21)\n");
    printf("                                 emphasizes vga gains over lna and mixer gains\n");
    printf("                                 mutually exclusive with all other gain settings\n");
    printf("                                 same as setting --gain\n");
    printf("--airspy-sensitivity-gain <gain> set sensitivity gain presets (Range 0-21)\n");
    printf("                                 emphasizes lna and mixer gains over vga gain\n");
    printf("                                 mutually exclusive with all other gain settings\n");
    printf("--airspy-enable-lna-agc          enable on lna agc\n");
    printf("--airspy-enable-mixer-agc        enable mixer agc\n");
    printf("--airspy-enable-packing          enable packing on the usb interface\n");
    printf("--airspy-enable-rf-bias          enable the bias-tee for external LNA\n");
    printf("\n");
}

static void show_config()
{
    fprintf(stderr, "serial           : 0x%" PRIx64 "\n", AirSpy.serial);
    fprintf(stderr, "freq             : %d\n", Modes.freq);
    fprintf(stderr, "sample-rate      : %.0f\n", Modes.sample_rate);
    fprintf(stderr, "sample-format    : %s\n", formatGetName(Modes.sample_format));
    fprintf(stderr, "\n");
    fprintf(stderr, "lna_gain         : %d %s\n", AirSpy.lna_gain, AirSpy.lna_gain < 0 ? "(not set)": "");
    fprintf(stderr, "mixer_gain       : %d %s\n", AirSpy.mixer_gain, AirSpy.mixer_gain < 0 ? "(not set)": "");
    fprintf(stderr, "vga_gain         : %d %s\n", AirSpy.vga_gain, AirSpy.vga_gain < 0 ? "(not set)": "");
    fprintf(stderr, "linearity_gain   : %d %s\n", AirSpy.linearity_gain,
        AirSpy.linearity_gain < 0 ? "(not set)": (AirSpy.linearity_gain * 10 == Modes.gain ? "(from --gain)" :""));
    fprintf(stderr, "sensitivity_gain : %d %s\n", AirSpy.sensitivity_gain, AirSpy.sensitivity_gain < 0 ? "(not set)": "");
    fprintf(stderr, "\n");
    fprintf(stderr, "lna_agc    : %s\n", AirSpy.lna_agc ? "on" : "off");
    fprintf(stderr, "mixer_agc  : %s\n", AirSpy.mixer_agc ? "on" : "off");
    fprintf(stderr, "packing    : %s\n", AirSpy.packing ? "on" : "off");
    fprintf(stderr, "rf_bias    : %s\n", AirSpy.rf_bias ? "on" : "off");
}

#define SET_PARAM_VAL(__name, __val) \
    status = airspy_set_ ## __name(AirSpy.device, __val); \
    if (status != 0) { \
        fprintf(stderr, "AirSpy: airspy_set_" #__name " failed with code %d\n", status); \
        airspy_close(AirSpy.device); \
        return false; \
    }

#define SET_PARAM(__name) \
    SET_PARAM_VAL(__name, AirSpy.__name)

#define SET_PARAM_GAIN(__name) \
    if (AirSpy.__name >= 0) { \
        SET_PARAM(__name); \
    }

bool airspyOpen()
{
    int status;

    if (AirSpy.device) {
        return true;
    }

    if (Modes.dev_name) {
        char *ptr = Modes.dev_name;
        if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
            ptr += 2;
        }
        if (sscanf(ptr, "%" PRIx64, &AirSpy.serial) != 1) {
            fprintf(stderr, "AirSpy: invalid device '%s'\n", Modes.dev_name);
            return false;
        }
    }

    if (Modes.gain != MODES_MAX_GAIN) {
        if (AirSpy.individual_gains_set || AirSpy.preset_gains_set || AirSpy.agcs_set) {
            fprintf(stderr, "AirSpy: --gain can't be combined with AirSpy specific gain or agc settings\n");
            return false;
        }
        AirSpy.linearity_gain = Modes.gain / 10;
        if (AirSpy.linearity_gain < 0 || AirSpy.linearity_gain > 21) {
            fprintf(stderr, "Error: --airspy-linearity-gain (or --gain) range is 0 - 21\n");
            return false;
        }
        AirSpy.preset_gains_set++;
    }

    if (AirSpy.individual_gains_set && AirSpy.preset_gains_set) {
        fprintf(stderr, "AirSpy: Individual gains can't be combined with preset gains\n");
        return false;
    }

    if ((AirSpy.lna_gain >= 0 || AirSpy.preset_gains_set) && AirSpy.lna_agc) {
        fprintf(stderr, "AirSpy: Options that alter lna-gain can't be combined with lna-agc\n");
        return false;
    }

    if ((AirSpy.mixer_gain >= 0 || AirSpy.preset_gains_set) && AirSpy.mixer_agc) {
        fprintf(stderr, "AirSpy: Options that alter mixer-gain can't be combined with mixer-agc\n");
        return false;
    }

    if (AirSpy.preset_gains_set > 1) {
        fprintf(stderr, "AirSpy: linearity-gain and sensitivity-gain are mutually exclusive\n");
        return false;
    }

    if (!(AirSpy.individual_gains_set || AirSpy.preset_gains_set)) {
        fprintf(stderr, "AirSpy: linearity-gain set to default of 21\n");
        AirSpy.linearity_gain = 21;
    }

    airspy_init();

    if (AirSpy.serial) {
        status = airspy_open_sn(&AirSpy.device, AirSpy.serial);
    } else {
        status = airspy_open(&AirSpy.device);
    }

    if (status != AIRSPY_SUCCESS) {
        fprintf(stderr, "AirSpy: airspy_open failed with code %d\n", status);
        return false;
    }

    SET_PARAM_GAIN(lna_gain);
    SET_PARAM_GAIN(mixer_gain);
    SET_PARAM_GAIN(vga_gain);
    SET_PARAM_GAIN(linearity_gain);
    SET_PARAM_GAIN(sensitivity_gain);

    SET_PARAM(lna_agc);
    SET_PARAM(mixer_agc);

    SET_PARAM(rf_bias);
    SET_PARAM(packing);

    int airspy_sample_format = AIRSPY_SAMPLE_UINT16_REAL;
    switch (Modes.sample_format) {
    case INPUT_SC16:
        airspy_sample_format = AIRSPY_SAMPLE_INT16_IQ;
        break;
    case INPUT_S16:
        airspy_sample_format = AIRSPY_SAMPLE_INT16_REAL;
        break;
    case INPUT_U16O12:
        airspy_sample_format = AIRSPY_SAMPLE_UINT16_REAL;
        break;
    default:
        fprintf(stderr, "AirSpy: Unsupported sample format specified.  Must be one of 'sc16', 's16', 'u16o12'\n");
        airspyClose();
        return false;
    }

    SET_PARAM_VAL(sample_type, airspy_sample_format);

    status = airspy_set_samplerate(AirSpy.device, (uint32_t)Modes.sample_rate);
    if (status != 0) {
        fprintf(stderr, "AirSpy: Invalid combination of sample rate (%.1f) and sample format (%s)\n",
            Modes.sample_rate, formatGetName(Modes.sample_format));
        airspyClose();
        exit (1);
    }

    status = airspy_set_freq(AirSpy.device, (uint32_t)Modes.freq);
    if (status != 0) {
        fprintf(stderr, "AirSpy: Set frequency (%d) failed\n", Modes.freq);
        airspyClose();
        exit (1);
    }

    show_config();

    AirSpy.converter = init_converter(Modes.sample_format,
                                      Modes.sample_rate,
                                      0,
                                      &AirSpy.converter_state);
    if (!AirSpy.converter) {
        fprintf(stderr, "AirSpy: can't initialize sample converter\n");
        return false;
    }

    bytes_per_sample = formatGetBytesPerSample(Modes.sample_format);

    return true;
}

/*!
 * This function is called by a thread in libairspy.
 */
static int handle_airspy_samples(airspy_transfer *transfer)
{
    sdrMonitor();

    if (Modes.exit || transfer->sample_count <= 0)
        return -1;

    static uint64_t dropped = 0;
    static uint64_t sampleCounter = 0;

    if (sampleCounter == 0) {
        set_thread_name("dump1090-airspy");
    }

    if (transfer->dropped_samples) {
        fprintf(stderr, "AirSpy: Driver Dropped %"PRIu64" samples\n",
            transfer->dropped_samples);
    }
    dropped += transfer->dropped_samples;

    struct mag_buf *outbuf = fifo_acquire(0 /* don't wait */);
    if (!outbuf) {
        // FIFO is full. Drop this block.
        dropped += transfer->sample_count;
        fprintf(stderr, "AirSpy: FIFO FULL!  Dropped %d samples.  Current Dropped: %"PRIu64"\n",
            transfer->sample_count, dropped);
        sampleCounter += transfer->sample_count;
        return 0;
    }

    outbuf->flags = 0;

    if (dropped) {
        // We previously dropped some samples due to no buffers being available
        outbuf->flags |= MAGBUF_DISCONTINUOUS;
        outbuf->dropped = dropped;
    } else {
        outbuf->dropped = 0;
    }

    dropped = 0;

    // Compute the sample timestamp and system timestamp for the start of the block
    outbuf->sampleTimestamp = sampleCounter * 12e6 / Modes.sample_rate;
    sampleCounter += transfer->sample_count;

    // Get the approx system time for the start of this block
    uint64_t block_duration = 1e3 * transfer->sample_count / Modes.sample_rate;
    outbuf->sysTimestamp = mstime() - block_duration;

    // Convert the new data
    unsigned to_convert = transfer->sample_count;
    if (to_convert + outbuf->overlap > outbuf->totalLength) {
        // how did that happen?
        to_convert = outbuf->totalLength - outbuf->overlap;
        dropped = transfer->sample_count - to_convert;
    }

    AirSpy.converter(transfer->samples, &outbuf->data[outbuf->overlap], to_convert,
        AirSpy.converter_state, &outbuf->mean_level, &outbuf->mean_power);

    outbuf->validLength = outbuf->overlap + to_convert;

    // Push to the demodulation thread
    fifo_enqueue(outbuf);

    return 0;
}

void airspyRun()
{
    if (!AirSpy.device) {
        fprintf(stderr, "airspyRun: AirSpy.device = NULL\n");
        return;
    }

    int status = airspy_start_rx(AirSpy.device, &handle_airspy_samples, NULL);
    if (status != 0) {
        fprintf(stderr, "airspy_start_rx failed\n");
        airspyClose();
        exit (1);
    }

    // airspy_start_rx does not block so we need to wait until the streaming is finished
    // before returning from the airspyRun function
    while (AirSpy.device && airspy_is_streaming(AirSpy.device) == AIRSPY_TRUE && !Modes.exit) {
        struct timespec slp = { 0, 100 * 1000 * 1000};
        nanosleep(&slp, NULL);
    }

    if (!Modes.exit) {
        fprintf(stderr, "aurspyRun: stopped streaming, probably lost the USB device, bailing out\n");
    }
}

void airspyStop()
{
    if (!AirSpy.device) {
        return;
    }
    airspy_stop_rx(AirSpy.device);
}

void airspyClose()
{
    struct airspy_device *device = AirSpy.device;
    AirSpy.device = NULL;

    if (!device) {
        return;
    }
    airspy_close(device);
    airspy_exit();
}


double airspyGetDefaultSampleRate()
{
    return 12e6;
}

input_format_t airspyGetDefaultSampleFormat()
{
    return INPUT_U16O12;
}

demodulator_type_t airspyGetDefaultDemodulatorType()
{
    return DEMOD_HIRATE;
}
