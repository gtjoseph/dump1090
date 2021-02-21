// Part of dump1090, a Mode S interface for AirSpy devices.
//
// sdr_airspy.h: AirSpy support (header)
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

#ifndef AIRSPY_H
#define AIRSPY_H

void airspyInitConfig();
void airspyShowHelp();
bool airspyHandleOption(int argc, char **argv, int *jptr);
bool airspyOpen();
void airspyRun();
void airspyClose();
double airspyGetDefaultSampleRate();
input_format_t airspyGetDefaultSampleFormat();
demodulator_type_t airspyGetDefaultDemodulatorType();

#endif
