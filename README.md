
-------------------------------------------------------------------------
Pluto gadget: dab2fm,  from DAB(+) to  stereo FM using the Pluto
-------------------------------------------------------------------------

For the real hobbyist, nothing is better than the sound of an old(er) radio,
preferably one with tubes in a nice, wooden cabinet.
From the early 60-ies, there are plenty of stereo radios, built with tubes,
on the (second hand) market (or somehwere on the attick)

The medium waves, while not completely empty, are hardly occupied with
regional or national transmissions, and from time to time one hears rumours
that the end of broadcasting in FM is also nearing. So, most of the
real beautiful wooden radios are basically now, or in the near future,
useless.

On the other hand, the development of digital radio goes fast.
Recently, here in the Netherlands, next to national and regional DAB
transmissions, also local DAB transmissions started.

Currently, I can receive around 5 or 6 ensembles, with in total
over 60 services on a simple whip antenna, with e.g. the Adalm pluto or the SDRplay
as SDR device, using my software.

The *Adalm Pluto* is a device with both receive and transmit capabilities.
In the process of "learning", after all, it is an *active learning module*,
I extended the command line version of the dab decoder with a module
to transmit the audio output on a user defined frequency,
on the same pluto device.Since DAB audio services are usually in stereo, and equipped with a
dynamic label, the audio of the selected service is re-transmitted
as a stereo signal.

While the picturen shows the reception of the transmitted signal
using my fm decoding software (with the SDRplay and the pluto close to each other),
attaching a (small) antenna makes reception with a nice wooden cabinet tube radio
with FM stereo possible.

![fm receiver](/dab-to-fm.png?raw=true)

The experiment relates mainly to finding the right (relative) values
for constructing the FM vsignal, especially the parameter to boost the
rds part of the signal.

----------------------------------------------------------------------
The program
-----------------------------------------------------------------------

The program is derived from the dab-cmdline/dab-library set of programs.
All non-audio backend stuff was removed, some code cleanup was done
and a closer coupling from the device to the DAB handling part was made.

This "improved" version of dab-cmdline supports only the Adalm Pluto
and - next to making the audio audible using the built-in soundcard
of the computer on which the program runs - transmits the audio - extended
with the text of the dynamic label as RDS message on a frequency, set
in the command line.

	dab-pluto-fm -C 12C -P "NPO Radio 4" -Q -F 110000

The parameters are mostly as in the examples for dab-cmdline,

	a. -C for the specification of the channel;
	b. -P for the specification of the audio service;
	c. -G for setting the gain of the input device, or
	d. -Q for setting the agc of the input device;
	e. -F for setting the output frequency (in KHz).
	f. -S for audio out next to transmitting the audio (default off).

Other parameters are

	g. -d for specifying the maximum amount of time to wait for time synchronization;
	h. -D for specifying the maximul amount of time to wait for detecting an ensemble;

---------------------------------------------------------------------------
Building an executable
--------------------------------------------------------------------------

Assuming the required libraries are installed, building an executable
is using cmake

mkdir build
cd build
cmake .. 
make

Note that to support the pluto, one needs to have installed

	a. libiio
	b. libad9361

Note that the version of libad9361 that is supported on older Linux distros
is an **incomplete** one.

--------------------------------------------------------------------------
Technical details
--------------------------------------------------------------------------

The output of the DAB decoder is 48000 samples/second, an FM constellation
is built on 192000 Hz. The first step is filtering the audio signal
so it is limited to 15 Khz.
The second step is upsampling to 192 Khz (with filtering), which happily is a multiple of
48000.
Then the signal is built (all computations with I/Q samples)

	The leftchannel + right channel signals on an IF 0;

	the Pilot signal on 19 Khz;

	the difference, i.e. left channel - right channel, modulated on a
	38 Khz carrier (twice the pilot);

	the RDS signal modulated on a 57 KHz signal (three times the pilot)

The resulting signal is then upsampled (with filtering) to 2112000.
The Pluto can handle samplerates from app 2083000 and higher and 2112000 is a
nice multiple of 192000.


