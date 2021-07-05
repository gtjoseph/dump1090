# HiRate Demodulator

In addition to the original "2400" demodulator used for a sample rate
of 2.4MHz, this build includes a new "hirate" demodulator for use with
samples rates above 3.0MHz. The new demodulator is needed for AirSpy
device support and can be used for other SDRs that also support sample
rates in excess of 2.4HMz.

Which demod is used is determined by the `--demod` command line option:

`--demod 2400`: The original 2.4MS/s demodulator.  This is the default for all SDRs except the AirSpy.

`--demod hirate`: A demodulator that can run at arbitrary sample rates above 3.0MS/s.  This is the default for the AirSpy SDR.

## Options for the hirate demodulator

|Option......................................................|Description|multi?|hirate?|
|:--------------|:-|:-:|:-:|
| `--demod-smoother-window <n>`|The number of samples over which a running average is taken. The default is the number of samples per symbol|Y|Y|
|`--demod-preamble-threshold <n>`|For a preamble to be detected, it's power has to be at least `<n>`db over the average power of the buffer that's being processed.<br>Default is 3.0db|Y|Y|
|`--demod-premble-window <l>:<h>`|The window of samples a preamble is searched for. `<l>` and `<h>` are specified with respect to the _expected_ start of a preamble. For example `--demod-premble-window -2:2` would seach from 2 samples before the expected start to 2 samples after the expected start of a preamble. The defaults for `<l>` and `<h>` are -samples_per_symbol and samples_per_symbol respectively. You can diable the search altogether with `0:0` |Y|Y|
|`--demod-preamble-strictness <n>`|How strict the test for a preamble should be. `0`=least strict.  `3`=most strict.|Y|Y|
|`--demod-msg-window <l>:<h>`|The window of samples a message is searched for. `<l>` and `<h>` are specified with respect to the _expected_ start of a message. For example `--demod-msg-window -2:2` would seach from 2 samples before the expected start to 2 samples after the expected start of a message. The defaults for `<l>` and `<h>` are both `0` (disabled). |n|Y|
|`--demod-no-mark-limits`|Normally a symbol in the message data will only be considered a 'mark' if it falls between the preamble average mark level \* 0.707 and \* 1.414. Setting this option will cause a symbol to be considered a 'mark' as long as it's greater than it's accompanying 'space'.|n|Y|


## Tuning
**All** of the above options will have some tradeoff between CPU utilization and number of messages decoded.  Only you can decide what the right combination of options is for you.  If you can, use your SDRs utilities to capture 10 seconds worth of samples and use dump1090's `ifile` virtual SDR to play those samples with various options.  Use the `--stats` option to see summary statistics at the end of the file.  

Some good practices:

 * The smoother window should be <= the number of samples per symbol.  At a 12Ms/s rate, a smoother window of 4 (as opposed to 6) seems to work better.
 * The preamble threshold seems to work well at the 3.0db default but try slightly lower or higher and see what happens to CPU utilization and decoded message count.
 * You may get better message counts with a narrower, or even no, preamble window.  If you use the `--stats` option, a table will be printed that shows how many preambles were detected at each position in the window. 
 * A low preamble strictness may result in more messages decoded but will raise CPU slightly.
 * Using the message window can result in more messages being decoded than using the preamble window.  Again, if you use the `--stats` option, a table will be printed that shows how many messages were decoded at each position in the window. 
 
**WARNING**:  For the most part, CPU utilization will be _inversely_ proportional to the actual message rate.  It takes more CPU to _search_ for a message than it does to actually decode one so you may see CPU utilization go _up_ during quiet periods.

## Reference:

|||
|-|-|
|ADSB symbol rate|2Msy/s|
|Symbol Levels| `mark` = high signal level<br>`space` = low signal level|
|Symbols per preamble|16|
|Symbols per bit|2|
|Manchester Encoding|`<symbol-a><symbol-b>` = `<bit>`<br>`mark-space` = `1`<br>`space-mark` = `0`<br>`space-space` = `violation`<br>`mark-mark` = `violation`|
|ADSB decoded bit rate|1Mb/s|
|Frame|`<preamble><message>`|
|Preamble Symbols|`1010000101000000` (not valid Manchester encoding on purpose)|
|Symbols per message|112 or 224 (valid Manchester encoding)|
|Bits per message|56 or 112|
|Bytes per message|7 or 14|

|Sample Rate (MS/s)|Samples per Symbol|
|-|-|
|2.0|1|
|2.4|1.2|
|4.0|2|
|5.0|2.5|
|6.0|3|
|8.0|4|
|10.0|5|
|12.0|6|
|20.0|10|
|24.0|12|


## Example Frame @ 12MS/s

Samples (6 samples per symbol)
The smoother creates a running average of a number of samples. With `--demod-smoother-window 4`...

```
   0         1         2         3         4         5         6         7         8         9         1
   012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
   |average of 0-4
    |average of 1-5
     |average of 2-6
     etc.  
```
Symbol value is determined by the averaged value every n samples per symbol (6 in this example).

```
   0         1         2         3         4         5         6         7         8         9         1
   012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
   |--0--|--1--|--2--|--3--|--4--|--5--|--6--|--7--|--8--|--9--|--10-|--11-|--12-|--13-|--14-|--15-|--16-|--17-|
   ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     
   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   
   |------------------------------------   preamble   ---------------------------------------------|--- message -->
   |mark |space|mark |space|space|space|space|mark |space|mark |space|space|space|space|space|space|
```
When searching for the preamble we start at a few samples before the expected start, and
continue for a few samples beyond the expected start as specified by the `--demod-preamble-window`
parameter.  With `--demod-preamble-window -2:2` 

```
   0         1         2         3         4         5         6         7         8         9         1
789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
 l X h 
```
The average sample with the highest value marks the start of the preamble.  If the highest value in the window was 2 samples before the eXpected start, we'd adjust as follows...

```
   0         1         2         3         4         5         6         7         8         9         1
789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
 |--0--|--1--|--2--|--3--|--4--|--5--|--6--|--7--|--8--|--9--|--10-|--11-|--12-|--13-|--14-|--15-|--16-|--17-|
 ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     
 Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   Sym   
 |------------------------------------   preamble   ---------------------------------------------|--- message -->
 |mark |space|mark |space|space|space|space|mark |space|mark |space|space|space|space|space|space|
```
The hirate demodulator also applies the same search to the start of the message using `--demod-msg-window`.






