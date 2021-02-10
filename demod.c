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

// Thses must be in the same order as demodulator_type_t
static demodulator_t demods[] = {
    { "2400", " Default 2.4 MS/s demodulator", DEMOD_2400, demodulate2400, demodulate2400Init, demodulate2400Free, NULL, NULL},
    { NULL, "NULL", DEMOD_NONE, NULL, NULL, NULL, NULL, NULL }
};

demodulator_t *demodGetByType(demodulator_type_t demod_type)
{
    if (demod_type > DEMOD_NONE) {
        return NULL;
    }
    return &demods[demod_type];
}

demodulator_t *demodGetByName(const char *name)
{
    int i;

    for (i = 0; i < DEMOD_NONE; i++) {
        if (strcasecmp(name, demods[i].name) == 0) {
            return &demods[i];
        }
    }

    return NULL;
}

const char *demodGetName(demodulator_type_t demod_type)
{
    if (demod_type > DEMOD_NONE) {
        return NULL;
    }
    return demods[demod_type].name;
}

bool demodHandleOption(int argc, char **argv, int *jptr)
{
    int i;
    bool handled = false;
    int j = *jptr;
    int more = j+1 < argc; // There are more arguments

    if (!strcmp(argv[j], "--demod") && more) {
        Modes.demod = demodGetByName(argv[++j]);
        if (!Modes.demod) {
            fprintf(stderr, "warning: --demod '%s' is unknown.  Will use default for device type.\n", argv[j]);
            fprintf(stderr, "Supported demodulators:\n");
            for (int i = 0; demods[i].name; ++i) {
                fprintf(stderr, "  %s\n", demods[i].name);
            }
            return false;
        }
        *jptr += 1;
        return true;
    }

    for (i = 0; !handled && demods[i].name; i++) {
        if (demods[i].demod_handle_option_fn) {
            handled =  demods[i].demod_handle_option_fn(argc, argv, jptr);
        }
    }

    return handled;
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

    for (i = 0; demods[i].name; i++) {
        if (demods[i].demod_show_help_fn) {
            demods[i].demod_show_help_fn();
        }
    }
}
