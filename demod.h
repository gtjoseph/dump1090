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

typedef enum { DEMOD_2400 = 0, DEMOD_NONE } demodulator_type_t;
typedef void (* demod_fn_t)(struct mag_buf *mag);
typedef void (* demod_init_fn_t)(void *);
typedef void (* demod_free_fn_t)(void *);

typedef struct {
    const char *name;
    demodulator_type_t demod_type;
    demod_fn_t demod_fn;
    demod_init_fn_t demod_init_fn;
    demod_free_fn_t demod_free_fn;
} demodulator_t;

demodulator_t *demodGetByType(demodulator_type_t demod_type);
demodulator_t *demodGetByName(const char *name);
const char *demodGetName(demodulator_type_t demod_type);

#endif
