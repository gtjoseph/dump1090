// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// sdr.c: generic SDR infrastructure
//
// Copyright (c) 2016-2017 Oliver Jowett <oliver@mutability.co.uk>
// Copyright (c) 2017 FlightAware LLC
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

#include "sdr_ifile.h"
#ifdef ENABLE_RTLSDR
#  include "sdr_rtlsdr.h"
#endif
#ifdef ENABLE_BLADERF
#  include "sdr_bladerf.h"
#endif
#ifdef ENABLE_HACKRF
#  include "sdr_hackrf.h"
#endif
#ifdef ENABLE_LIMESDR
#  include "sdr_limesdr.h"
#endif
#ifdef ENABLE_AIRSPY
#  include "sdr_airspy.h"
#endif

typedef struct {
    const char *name;
    sdr_type_t sdr_type;
    void (*initConfig)();
    void (*showHelp)();
    bool (*handleOption)(int, char**, int*);
    bool (*open)();
    void (*run)();
    void (*close)();
    double (*getDefaultSampleRate)();
    input_format_t (*getDefaultSampleFormat)();
    demodulator_type_t(*getDefaultDemodulatorType)();
} sdr_handler;

static void noInitConfig()
{
}

static void noShowHelp()
{
}

static bool noHandleOption(int argc, char **argv, int *jptr)
{
    MODES_NOTUSED(argc);
    MODES_NOTUSED(argv);
    MODES_NOTUSED(jptr);

    return false;
}

static bool noOpen()
{
    fprintf(stderr, "Net-only mode, no SDR device or file open.\n");
    return true;
}

static void noRun()
{
}

static void noClose()
{
}

static double noSampleRate()
{
    return 2400000.0f;
}

static input_format_t noSampleFormat()
{
    return INPUT_UC8;
}

static demodulator_type_t noDemodulatorType()
{
    return DEMOD_2400;
}

static bool unsupportedOpen()
{
    fprintf(stderr, "Support for this SDR type was not enabled in this build.\n");
    return false;
}

static sdr_handler sdr_handlers[] = {
#ifdef ENABLE_RTLSDR
    { "rtlsdr", SDR_RTLSDR, rtlsdrInitConfig, rtlsdrShowHelp, rtlsdrHandleOption, rtlsdrOpen, rtlsdrRun, rtlsdrClose, rtlsdrGetDefaultSampleRate, rtlsdrGetDefaultSampleFormat, rtlsdrGetDefaultDemodulatorType },
#endif

#ifdef ENABLE_BLADERF
    { "bladerf", SDR_BLADERF, bladeRFInitConfig, bladeRFShowHelp, bladeRFHandleOption, bladeRFOpen, bladeRFRun, bladeRFClose, bladeRFGetDefaultSampleRate, bladeRFGetDefaultSampleFormat, bladeRFGetDefaultDemodulatorType },
#endif

#ifdef ENABLE_HACKRF
    { "hackrf", SDR_HACKRF, hackRFInitConfig, hackRFShowHelp, hackRFHandleOption, hackRFOpen, hackRFRun, hackRFClose, hackRFGetDefaultSampleRate, hackRFGetDefaultSampleFormat, hackRFGetDefaultDemodulatorType },
#endif
#ifdef ENABLE_LIMESDR
    { "limesdr", SDR_LIMESDR, limesdrInitConfig, limesdrShowHelp, limesdrHandleOption, limesdrOpen, limesdrRun, limesdrClose, limesdrGetDefaultSampleRate, limesdrGetDefaultSampleFormat, limesdrGetDefaultDemodulatorType },
#endif
#ifdef ENABLE_AIRSPY
    { "airspy", SDR_AIRSPY, airspyInitConfig, airspyShowHelp, airspyHandleOption, airspyOpen, airspyRun, airspyClose, airspyGetDefaultSampleRate, airspyGetDefaultSampleFormat, airspyGetDefaultDemodulatorType },
#endif

    { "none", SDR_NONE, noInitConfig, noShowHelp, noHandleOption, noOpen, noRun, noClose, noSampleRate, noSampleFormat, noDemodulatorType },
    { "ifile", SDR_IFILE, ifileInitConfig, ifileShowHelp, ifileHandleOption, ifileOpen, ifileRun, ifileClose, ifileGetDefaultSampleRate, ifileGetDefaultSampleFormat, ifileGetDefaultDemodulatorType },

    { NULL, SDR_NONE, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL } /* must come last */
};

void sdrInitConfig()
{
    // Default SDR is the first type available in the handlers array.
    Modes.sdr_type = sdr_handlers[0].sdr_type;

    for (int i = 0; sdr_handlers[i].name; ++i) {
        sdr_handlers[i].initConfig();
    }
}

void sdrShowHelp()
{
    printf("--device-type <type>     Select SDR type (default: %s)\n", sdr_handlers[0].name);
    printf("\n");

    for (int i = 0; sdr_handlers[i].name; ++i) {
        sdr_handlers[i].showHelp();
    }
}

bool sdrHandleOption(int argc, char **argv, int *jptr)
{
    int j = *jptr;
    if (!strcmp(argv[j], "--device-type")) {
        if ((j+1) < argc) {
            ++j;
            for (int i = 0; sdr_handlers[i].name; ++i) {
                if (!strcasecmp(sdr_handlers[i].name, argv[j])) {
                    Modes.sdr_type = sdr_handlers[i].sdr_type;
                    *jptr = j;
                    return true;
                }
            }
            fprintf(stderr, "SDR type '%s' not recognized. ", argv[j]);
        }

        fprintf(stderr, "Supported SDR types:\n");
        for (int i = 0; sdr_handlers[i].name; ++i) {
            fprintf(stderr, "  %s\n", sdr_handlers[i].name);
        }

        return false;
    }

    for (int i = 0; sdr_handlers[i].name; ++i) {
        // If device type has already been specified on the command line, only
        // call that types option handler.  This prevents one type from eating
        // options meant for another.
        if (Modes.sdr_type != SDR_NONE) {
            if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
                if (sdr_handlers[i].handleOption(argc, argv, jptr)) {
                    return true;
                }
            }
        } else if (sdr_handlers[i].handleOption(argc, argv, jptr)) {
            return true;
        }
    }

    return false;
}

static sdr_handler *current_handler()
{
    static sdr_handler unsupported_handler = { "unsupported", SDR_NONE, noInitConfig, noShowHelp, noHandleOption, unsupportedOpen, noRun, noClose, noSampleRate, noSampleFormat, noDemodulatorType };

    for (int i = 0; sdr_handlers[i].name; ++i) {
        if (Modes.sdr_type == sdr_handlers[i].sdr_type) {
            return &sdr_handlers[i];
        }
    }

    return &unsupported_handler;
}

bool sdrOpen()
{
    pthread_mutex_init(&Modes.reader_cpu_mutex, NULL);
    return current_handler()->open();
}

void sdrRun()
{
    set_thread_name("dump1090-sdr");

    pthread_mutex_lock(&Modes.reader_cpu_mutex);
    Modes.reader_cpu_accumulator.tv_sec = 0;
    Modes.reader_cpu_accumulator.tv_nsec = 0;
    start_cpu_timing(&Modes.reader_cpu_start);
    pthread_mutex_unlock(&Modes.reader_cpu_mutex);

    current_handler()->run();

    pthread_mutex_lock(&Modes.reader_cpu_mutex);
    end_cpu_timing(&Modes.reader_cpu_start, &Modes.reader_cpu_accumulator);
    pthread_mutex_unlock(&Modes.reader_cpu_mutex);
}

void sdrClose()
{
    pthread_mutex_destroy(&Modes.reader_cpu_mutex);
    current_handler()->close();
}

double sdrGetDefaultSampleRate()
{
    return current_handler()->getDefaultSampleRate();
}

input_format_t sdrGetDefaultSampleFormat() {
    return current_handler()->getDefaultSampleFormat();
}

demodulator_type_t sdrGetDefaultDemodulatorType() {
    return current_handler()->getDefaultDemodulatorType();
}

void sdrMonitor()
{
    pthread_mutex_lock(&Modes.reader_cpu_mutex);
    update_cpu_timing(&Modes.reader_cpu_start, &Modes.reader_cpu_accumulator);
    pthread_mutex_unlock(&Modes.reader_cpu_mutex);
}

void sdrUpdateCPUTime(struct timespec *addTo)
{
    pthread_mutex_lock(&Modes.reader_cpu_mutex);
    add_timespecs(&Modes.reader_cpu_accumulator, addTo, addTo);
    Modes.reader_cpu_accumulator.tv_sec = 0;
    Modes.reader_cpu_accumulator.tv_nsec = 0;
    pthread_mutex_unlock(&Modes.reader_cpu_mutex);
}
