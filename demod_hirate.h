// Part of dump1090, a Mode S message decoder
//
// demod_hirate.h: Hi Rate Mode S demodulator prototypes.
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

#ifndef DUMP1090_DEMOD_HIRATE_H
#define DUMP1090_DEMOD_HIRATE_H

#include <stdint.h>
#include "demod.h"

void demodulateHiRate(struct mag_buf *mag);
int demodulateHiRateInit(demodulator_context_t *ctx);
void demodulateHiRateFree(void);
void demodulateHiRateHelp(void);

#endif
