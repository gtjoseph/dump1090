# Native AirSpy Support

As an alternative to using airspy_adsb, dump1090-fa can now access AirSpy devices
directly.  To use it, install the AirSpy libraries for your distribution.
For Debian based distributions, the package is "libairspy0".  For RedHat
based distributions, it's "airspyone_host".

## Options

### Device-specific options:
|Option|Parameter|Description|
|------|---------|-----------|
|--device|\<serial\>|Select device by hex serial number. This can be omitted if there's only 1 device connected to the system.|
|--lna-gain|\<gain\>|Set lna gain (Range 0-15)|
|--mixer-gain|\<gain\>|Set mixer gain (Range 0-15)|
|--vga-gain|\<gain\>|Set vga gain (Range 0-15)|
|--linearity-gain|\<gain\>|Set linearity gain presets (Range 0-21). (default 21)                               Emphasizes vga gains over lna and mixer gains and is mutually exclusive with all other gain settings. Same as setting --gain|
|--sensitivity-gain|\<gain\>|Set sensitivity gain presets (Range 0-21). Emphasizes lna and mixer gains over vga gain and is mutually exclusive with all other gain settings.|
|--enable-lna-agc||Enable lna agc|
|--enable-mixer-agc||Enable mixer agc|
|--enable-packing||Enable packing on the usb interface.  Will reduce USB bandwidth by about 24%|
|--enable-rf-bias||Enable the bias-tee for external LNA|

### Common dump1090 options that also apply:
|Option|Parameter|Description|
|------|---------|-----------|
|--sample-rate|\<rate\>|Sets the sample rate in Hz (12000000) or MHz (12)|
|--sample-format|\<format\>|Sets the sample format to one of 's16', 'u16o12' or 'sc16'.  See below.|

  
### Sample Formats:

* **s16** : Signed 16bit full scale.  We're still limited to the
ADC's 12-bit precision so there's no real advantage
to using this format. libairspy provides it though so
it's here for completeness.
    
* **u16o12** : Unsigned 16 bit with the sample shifted up by 12 bits
(2048) so the effective -2047 to +2047 resolution
becomes 1 - 4095. This is the native format and the
least intensive to process. It's also the default.
    
* **sc16** : Signed 16bit complex format, also known as IQ format.
This format can provide more accurate results but it
requires that the ADC run at twice the configured
sample rate with the corresponding doubling of USB
transfer rate. Therefore it's only available for the
6 and 12 MS/s sample rates.

### Sample Rate / Format Combinations:

|Sample Rate|Format|Description|
|-|-|-|
|6MS/s|s16|Not supported by the device.|
|6MS/s|u16o12|Not supported by the device.|
|6MS/s|sc16|As noted above, the ADC sample rate will be 12MS/s with the corresponding increase in USB transfer rate. This rate/format combination can actually provide better results and lower CPU utilization than the 12MS/s s16 and u16o12 rates/formats.|
|12MS/s|s16|No real advantage over u16o12.|
|12MS/s|u16o12|This is the AirSpy's native format.|
|12MS/s|sc16|USE WITH CAUTION!  As noted above the ADC sample rate will be 24MS/s with the corresponding USB transfer rate. This can easily overwhelm the USB bus.  At a minimum, --enable-packing should be used to reduce the USB rate.  There doesn't _seem_ to be any advantage to using this rate and format but some may find it useful.|
|20MS/s|s16|No real advantage over u16o12.|
|20MS/s|u16o12|Again, this is the AirSpy's native format but whether running at 20MS/s vs 12MS/s is better, only you can decide.|
|20MS/s|sc16|Not supported by the device.  The resulting USB transfer rate would exceed USB2 capability.|
|24MS/s|s16|No real advantage over u16o12.|
|24MS/s|u16o12|Again, this is the AirSpy's native format but whether running at 24MS/s vs 20MS/s or 12MS/s is better, only you can decide.|
|24MS/s|sc16|Not supported by the device.  The resulting USB transfer rate would exceed USB2 capability|
    
**Note**: The "hirate" demodulator is the default for this device. Trying to use the '2400' demodulator will result
in complete garbage.

## Building:
    
To build with AirSpy support, install the appropriate development
package for your distribution.  For Debian based distros, it's
named libairspy-dev.  For RedHat based distros, it's
airspyone_host-devel.  Simply run 'make' and the support should
be included.  Some distributions also have a libairspyhf package
but that's not for this device or purpose. 
    
Building with AirSpy support does *NOT* require the airspy
libraries to be installed on the runtime host if you aren't actually
using an AirSpy device.  The driver checks for the presense of the
libraries when it loads and if they're not found, AirSpy supoprt
is simply disabled.  This makes it safe to build a generic
executable that includes AirSpy support but can run on systems
without the libraries installed.
    
Final note:  Building with the clang compiler instead of gcc can
provide a noticable decrease in CPU utilization.  To use it, install
the latest clang package available for your distribution (it's
generally supported) and build with 'make CC=clang'.  Debian based
distros seem to include several clang versions with the latest NOT
being the default.  On Raspbian for instance, the latest is clang-9
but just running "clang" gets you clang-6.  You can just add the
appropriate version to the make command line like so:
'make CC=clang-9'.
