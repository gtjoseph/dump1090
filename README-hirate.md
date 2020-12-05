# HiRate Demodulator

The standard "2400" demodulator works great with the devices that can use a
2.4MHz sample rate but it's limited to that rate.  Devices that can use a
higher sample rate need this "hirate" demodulator.

## Background

### Encoding
To understand how this demodulator works, a refresher on ADS-B Mode S messages is in order.

To start with, the ADS-B data rate is 1 Mbit/s.  That's the bit rate at which the actual
"application" data is transmitted.  Raw application data is rarely the best format for
transmission over most media, especially a shared medium like RF.  For example, simply
sending the number zero could result in a long transmission of _nothing_ because the bits
are all 0.  This makes it difficult for the receiver to stay synchronized with the
transmitter and since RF is a shared medium, another transmitter could send data
during that time which would confuse the receiver even further.  To help alleviate
this issue, the application data is "Manchester" encoded.

Manchester encoding involves sending 2 "symbols" for each application bit.  A "mark",
which is the transmitter sending the 1090MHz carrier, and a "space" which is the transmitter
not sending anything.  If the bit to be sent is a "1", the transmitter sends a mark then a space.
If the bit to be sent is a "0", then it sends a space followed by a mark.  So if the
transmitter wants to send an 8-bit application "0" (00000000), it would actually send
0101010101010101.  If it wanted to send a "1" (00000001) it would send 0101010101010110.
Notice the order of the rightmost two symbols changed.  Since a bit can only ever be a
1 or 0, the transmitter can never send two marks or two spaces to represent a bit.
With this, some rules emerge...

* The symbol rate is twice the data rate: 2 Msymbols/s.
* There can never be 3 consecutive marks or 3 consecutive spaces in a message (except the preamble which we'll get to below)

This makes it easier for the receiver to recover the application data from the symbols.

### Format

Each Mode S frame consists of a 16 symbol preamble followed by a 112 symbol/56 bit message or a 
224 symbol/112 bit message.  The preamble is a special sequence that allows the receiver to detect
the start of a message.  It's not Manchester encoded on purpose.  The longer sequences of spaces
makes it easier for the receiver to recognize this as a preamble rather than the middle of some
other message.

     <-- preamble--><---- message ( 112 or 224 symbols ) ---->
    1010000101000000nnnnnnnn...nnnnnnnn...nnnnnnnn...nnnnnnnnn
    MsMssssMsMssssss
    ^_^____^_^______

In terms of time, based on the rates discussed above, a preamble takes 8us and the
message either 56us or 112us.

### Sampling

With the default RTLSDR sample rate of 2.4 Msamples/s, doing some quick math should reveal that
at that rate, the SDR can only sample a 16 sample/8us preamble about 19 times (6 samples per 5 symbols).
Because of this, the "2400" demodulator has to do some fancy footwork to get an accurate
representation of the signal.  Because the sample rate is so low however, it doesn't take a lot
of CPU resources.  With a higher sample rate, 12 Msamples/s for example, we get 6 samples for
_each_ symbol which should make getting an accurate representation dead simple but the downside
is that, since the samples are arriving 5 times faster, way more CPU resources are required.
In the end, we still have to do some fancy footwork to keep the CPU resources at an acceptable level.

## The Demodulator

The HiRate demodulator needs at least a sample rate of 6 Msamples/s to be effective.  This allows us to
sample each symbol 3 times.  This isn't optimal though because, depending on when we sample in relation to the start of the symbol, we may be sampling at the very edge of the sample, leaving only 2 samples somewhere inside the symbol.  This isn't really optimal, especially when trying to detect a preamble.  12 Msamples/s is a better
choice because, with 6 samples per symbol, we get more samples in the middle of the symbol.

### The Process

Regardless of which SDR is in use, the common sample format input to the demodulator is a 16 bit representation of the signal level.  0 = No signal.  65535 = full scale signal.  The samples in a "mark" symbol will have a higher value than the samples in a "space" symbol.  How much the difference will be depends on a lot of things, including the ambient noise floor and what other transmitters are sending at ther same time.  The buffer passed to the demodulator can be up to 128K samples at a time. Also passed into the demodulator is the mean signal level for the buffer.

1. To make detecting marks and spaces easier, the demodulator calculates a threshold noise level by multiplying
the mean signal level by a "threshold factor" you can provide on the command line with the `--hirate-threshold` option.  The default is `0.4`.
1. A pass is made over the entire buffer to do two things...
   1. Any sample that's lower than the calculated threshold is automatically forced to zero.
   1. A "running sum" is done on the buffer that summs a number of consecutive samples. You can specify the
number of samples to be summed with the `--hirate-window` option with the default being `3`.
So, for example, sample 0 will have the sum of itself plus samples 1 and 2.  Sample 1 will have the
sum of itself plus samples 2 and 3, etc.  These operations give us the best chance of accurately
differentiating marks and spaces.

Now we scan down the buffer looking for messages:

1. A preamble can't start with a sample that's below the threshold so we first scan down the buffer until we find any sample that's above the threshold.  If we don't find a sample above the threshold, there's nothing in this buffer to decode.
1. Once we find a sample above the threshold, we test the successive 16 samples to see if they match the
pattern described above and a score is assigned to that chunk.  `20` is a perfect score but is rarely
achieved.  The passing score can be supplied via the `--hirate-score` option.  The default is
`19`.  If we don't get a passing score, we advance by 1 sample and go back to the previous step of looking for
a sample above the threshold.
1. If we find a preamble, we advance by 16 symbols and put a stake in the ground called "message start".
We then start attempting to decode a message.  This is the tricky part. There's no guarantee that we
sampled the input signal at the exact start of a preamble.  In fact, we probably didn't.  There's not
really much to detecting the preamble however so being a bit off isn't a big deal.  Decoding a message
is harder though.  To get the best result, we need to go though a few hoops.  We actually start looking for
a good message a few samples _before_ the theoretical start of the message. I.E. `message_start` minus a few samples.  The "window" of samples we back up is controlled by the `--hirate-tries` option.  The default is
11 so we divide that by 2 which causes us to start at 5 samples  _before_ `message_start` and continue trying until we've advanced to 5 samples _after_ `message_start`.  This gives us a window of 11 samples.
   1. Each time we try, we start by looking for a valid Downlink Format code in the first byte.  If we don't
   find one, we advance one sample and look again.
   1. Each time we find a valid DF code, we score the message.
   1. We then advance 1 sample and do both steps again until we've reached the end of the window.
   1. Once we've scored all the messages in the window, we take the one with the best score and pass that
   to the core to be decoded.
   1. We then calculate the message's signal strength and update the stats.
1. Rinse and Repeat.

### The "knobs"

This demodulator can be used at different sample rates, in different environments, on different hardware, etc.
There's no way a single static set of parameters can provide the best results under all conditions.  Therefore,
the 4 command line options mentioned above are provides to allow the user to customize the process for their
environment.  Each one has a tradeoff between resources required and messages decoded though.

* `--hirate-threshold` (default 0.4): The lower you set this value the more sensitive the demodulator
will be to low strength signals.  In quieter environments, you may get more messages decoded but
with more samples above the threshold to test, the CPU utilization will be higher.  Conversely, setting the threshold higher may be useful in noisy environments and may allow you to decode more messages with _less_ CPU resources since there will be fewer samples above the threshold to test.
* `--hirate-window` (default 3): This controls how many samples are summed together to determine if a symbol
is a mark or a space.  The lower the value, the less work has to be done and the higher the value the more work has to be done.  Changing the value will affect the number of messages decoded but whether it's better or worse
can only be determined by the user testing.
* `--hirate-score` (default 19): This controls the score a preamble has to achieve before it's considered 
"good".  Setting it to `20` will cause only perfect preambles to be passed but you'll probably only get a handful.  Lowering it a point to `18` may get you 5%-10% more decoded messages but will probably cost you the 
same number of CPU percentage points.
* `--hirate-tries` (default 11): Oddly enough, the number of message decode tries has much less effect
on CPU utilization than the other 3 options.  Setting it to `7` may drop decoded messages by 5% with only a
1% drop in CPU utilization.  On the other hand, setting it to the maximum of `15` may do the reverse.

## Perfrormance Tips.

First, if you're compiling dump1090 from source, use the latest "clang" compiler you distribution
has.  Using it can save you 20% CPU at runtime over the same code compiled with gcc. Once you've
install clang, you can use it just by running `make CC=clang`.  On some distributions, you may
have to specify the exact clang version.  `make CC=clang-9` for instance.

The default settings were chosen as a "safe" compromize between decoded messages and CPU utilization
on 3 platforms... an x86_64 desktop, a Raspberry Pi 4 running 32-bit Raspian, and an Raspberry Pi 4
running 64-bit Raspian.  

Use your SDR's utilities to capture 60 seconds worth of samples to disk and use _that_ to test various
combinations of options using dump1090's "ifile" psaudo-SDR.  If you try to do it on a live stream, you
won't get repeatable results you can compare.  For instance, if you have an AirSpy device run...

```bash
$ airspy_rx -g 21 -a 12000000 -t 4 -p 1 -f 1090 -n 720000000 -r dump12-60.bin
$ dump1090-fa --device-type ifile --ifile dump12-60.bin --throttle --sample-rate 12 \
  --sample-format u16o12 --demod hirate --fix-2bit  --quiet --stats-every 1 \
  --hirate-score 18 --hirate-tries 15

```
Watch the `preambles received`, `total usable messages` and `CPU load` stats.
Also, when dump1090 ends, the demodulator will print its own stats just before dump1090
prints its final stats.  The stats will look like...
```
Demod Hirate:
Successful Message Decode Offsets
Decode Tries: 11
Offset   Count
 -5:         3
 -4:         8
 -3:        23
 -2:        45
 -1:       354
  0:      6966
  1:      4415
  2:       875
  3:       296
  4:       109
  5:        18
```
This can help you tune the `--hirate-tries` parameter.  In this test you can see that almost
7000 messages were decoded at the exact place we thought the message should start but 4414
were decoded when starting at 1 sample beyond that.  Conversely you can see that decreasing
the number of tries to 9 ( -4 to +4 ) would have lost you only 21 messages.

Finally... Don't be surprised that the CPU utilization will actually go _DOWN_ as the message
rate goes _UP_.  Most of the CPU time is spent _searching_ for preambles and DF codes, not
decoding actual messages.  Every time a message is successfulyl decoded, that's up to 1440
samples (at 12 Msamples/s) that don't have to be searched.  The most CPU it will use is when
there's no input signal. :)









