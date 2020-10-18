 
/*
 *    Copyright (C) 2020
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of the dab-pluto-fm
 *
 *    dab-pluto-fm is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    dab-pluto-fm is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with dab-pluto-fm; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include	<unistd.h>
#include	<signal.h>
#include	<getopt.h>
#include        <cstdio>
#include        <iostream>
#include	<complex>
#include	<vector>
#include	"audiosink.h"
#include	"dab-api.h"
#include	"dab-processor.h"
#include	"band-handler.h"
#include	"ringbuffer.h"
#include	"locking-queue.h"
#include	"pluto-handler.h"
#include	"dab-streamer.h"
#include	<locale>
#include	<codecvt>
#include	<atomic>
#include	<string>
using std::cerr;
using std::endl;

//
//	availability of audio is signalled by an "event"
typedef struct {
	int key;
	std::string string;
} message;

static
lockingQueue<message>messageQueue;

#define	S_QUIT		0100
#define	S_NEW_AUDIO	0101
#define	S_NEW_DYNAMICLABEL	0102

void    printOptions (void);	// forward declaration
//	we deal with callbacks from different threads. So, if you extend
//	the functions, take care and add locking whenever needed
static
std::atomic<bool> run;

static
dabProcessor	*theRadio	= nullptr;

static
std::atomic<bool>timeSynced;

static
std::atomic<bool>timesyncSet;

static
std::atomic<bool> localSound;
static
RingBuffer<std::complex<int16_t>> pcmBuffer (32768);

static
std::atomic<bool>ensembleRecognized;

static
callbacks	the_callBacks;
static
audioBase	*soundOut	= nullptr;
static
audioBase	*streamerOut	= nullptr;

std::string	programName	= "Sky Radio";

static void sighandler (int signum) {
	fprintf (stderr, "Signal caught, terminating!\n");
	run. store (false);
}

static
void	syncsignalHandler (bool b, void *userData) {
	timeSynced. store (b);
	timesyncSet. store (true);
	(void)userData;
}
//
//	This function is called whenever the dab engine has taken
//	some time to gather information from the FIC bloks
//	the Boolean b tells whether or not an ensemble has been
//	recognized, the names of the programs are in the 
//	ensemble
static
void	ensembleHandler (std::string name, int Id, void *userData) {
	fprintf (stderr, "ensemble %s is (%X) recognized\n",
	                          name. c_str (), (uint32_t)Id);
	ensembleRecognized. store (true);
}


std::vector<std::string> programNames;

#include	<bits/stdc++.h>

std::unordered_map <int, std::string> ensembleContents;
static
void	addtoEnsemble (std::string s, int SId, void *userdata) {
	for (std::vector<std::string>::iterator it = programNames.begin();
	             it != programNames. end(); ++it)
	   if (*it == s)
	      return;
	ensembleContents. insert (pair <int, std::string> (SId, s));
	programNames. push_back (s);
	std::cerr << "program " << s << " is part of the ensemble\n";
}

static
void	programdataHandler (audiodata *d, void *ctx) {
	(void)ctx;
	std::cerr << "\tstartaddress\t= " << d -> startAddr << "\n";
	std::cerr << "\tlength\t\t= "     << d -> length << "\n";
	std::cerr << "\tsubChId\t\t= "    << d -> subchId << "\n";
	std::cerr << "\tprotection\t= "   << d -> protLevel << "\n";
	std::cerr << "\tbitrate\t\t= "    << d -> bitRate << "\n";
}

//
//	The function is called from within the library with
//	a string, the so-called dynamic label
static
void	dynamicLabelHandler (std::string dynamicLabel, void *ctx) {
message m;
        m. key          = S_NEW_DYNAMICLABEL;
        m. string       = dynamicLabel;
        messageQueue. push (m);
}

static
void	handle_dynamicLabel	(std::string dynamicLabel) {
	streamerOut	-> addRds (dynamicLabel. c_str ());
	std::cerr << dynamicLabel << "\r";
}
//
//	The function is called from the MOT handler, with
//	as parameters the filename where the picture is stored
//	d denotes the subtype of the picture 
//	typedef void (*motdata_t)(std::string, int, void *);
void	motdataHandler (std::string s, int d, void *ctx) {
	(void)s; (void)d; (void)ctx;
//	fprintf (stderr, "plaatje %s\n", s. c_str ());
}

//
static
void	audioOutHandler (int rate, void *ctx) {
        message m;
        m. key          = S_NEW_AUDIO;
        m. string       = to_string (rate);
        messageQueue. push (m);
}

static
void	pcmHandler	(int rate) {
static bool isStarted	= false;

	if (!isStarted) {
	   soundOut	-> restart ();
	   isStarted	= true;
	}
	int16_t lbuf [1024];
	while (pcmBuffer. GetRingBufferReadAvailable () > 1024 / 2) {
	   pcmBuffer. getDataFromBuffer ((std::complex<int> *)lbuf, 512);
	   streamerOut	-> audioOut (lbuf, 1024, rate);
	   if (localSound. load ())
	      soundOut	-> audioOut (lbuf, 1024, rate);
	}
}

int	main (int argc, char **argv) {
// Default values
uint8_t		theMode		= 1;
std::string	theChannel	= "11C";
uint8_t		theBand		= BAND_III;
int16_t		gain		= 60;
bool		autogain	= false;
const char	*optionsString	= "T:SD:d:M:B:P:A:C:G:QF:";
int32_t		fmFrequency	= 109000000;
std::string	soundChannel	= "default";
int16_t		latency		= 10;
int16_t		timeSyncTime	= 10;
int16_t		freqSyncTime	= 5;
int		opt;
struct sigaction sigact;
bandHandler	dabBand;
deviceHandler	*theDevice	= nullptr;
bool	err;
int	theDuration		= -1;	// no limit
RingBuffer<std::complex<float>> _I_Buffer (16 * 32768);

	std::cerr << "dab_cmdline example,\n \
	                Copyright 2020 J van Katwijk, Lazy Chair Computing\n";
	timeSynced.	store (false);
	timesyncSet.	store (false);
	run.		store (false);
	localSound.	store (false);
//	std::wcout.imbue(std::locale("en_US.utf8"));
	if (argc == 1) {
	   printOptions ();
	   exit (1);
	}

	std::setlocale (LC_ALL, "en-US.utf8");

	fprintf (stderr, "options are %s\n", optionsString);
	while ((opt = getopt (argc, argv, optionsString)) != -1) {
	   switch (opt) {
	      case 'T':
	         theDuration	= atoi (optarg);	// seconds
	         break;

	      case 'S':
	         localSound. store (true);
	         fprintf (stderr, "local sound is ON\n");
	         break;

	      case 'D':
	         freqSyncTime	= atoi (optarg);
	         break;

	      case 'd':
	         timeSyncTime	= atoi (optarg);
	         break;

	      case 'M':
	         theMode	= atoi (optarg);
	         if (!((theMode == 1) || (theMode == 2) || (theMode == 4)))
	            theMode = 1; 
	         break;

	      case 'B':
	         theBand = std::string (optarg) == std::string ("L_BAND") ?
	                                                 L_BAND : BAND_III;
	         break;

	      case 'P':
	         programName	= optarg;
	         break;

	      case 'A':
	         soundChannel	= optarg;
	         break;

	      case 'G':
	         gain		= atoi (optarg);
	         break;

	      case 'Q':
	         autogain	= true;
	         break;

	      case 'C':
	         theChannel	= std::string (optarg);
	         fprintf (stderr, "%s \n", optarg);
	         break;

	      case 'F':
	         fmFrequency	= atoi (optarg);
	         break;

	      default:
	         fprintf (stderr, "Option %c not understood\n", opt);
	         printOptions ();
	         exit (1);
	   }
	}
//
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;

	the_callBacks. signalHandler            = syncsignalHandler;
        the_callBacks. ensembleHandler          = ensembleHandler;
	the_callBacks. programdataHandler	= programdataHandler;
        the_callBacks. audioOutHandler          = audioOutHandler;
        the_callBacks. addtoEnsemble		= addtoEnsemble;
        the_callBacks. dynamicLabelHandler      = dynamicLabelHandler;
        the_callBacks. motdataHandler           = motdataHandler;

	int32_t frequency	= dabBand. Frequency (theBand, theChannel);
	try {
	   theDevice	= new plutoHandler	(&_I_Buffer,
	                                         frequency,
	                                         gain, autogain,
	                                         fmFrequency
	                                        );

	}
	catch (int e) {
	   std::cerr << "allocating device failed (" << e << "), fatal\n";
	   exit (32);
	}
	if (theDevice == nullptr) {
	   fprintf (stderr, "no device selected, fatal\n");
	   exit (33);
	}
//
	streamerOut	= new dabStreamer (48000, 192000,
	                                   (plutoHandler *)theDevice);
	soundOut	= new audioSink	(latency, soundChannel, &err);
	if (err) {
	   std::cerr << "no valid sound channel, fatal\n";
	   exit (33);
	}
//
//	and with a sound device we now can create a "backend"
	theRadio	= new dabProcessor (&_I_Buffer,
	                                    &pcmBuffer,
	                                    &the_callBacks,
	                                    nullptr		// Ctx
	                          );
	if (theRadio == nullptr) {
	   std::cerr << "sorry, no radio available, fatal\n";
	   exit (4);
	}

	theRadio	-> start ();
	theDevice	-> restartReader (frequency);

	timesyncSet.		store (false);
	ensembleRecognized.	store (false);

	while (!timeSynced. load () && (--timeSyncTime >= 0))
	   sleep (1);

	if (!timeSynced. load ()) {
	   cerr << "There does not seem to be a DAB signal here" << endl;
	   theDevice -> stopReader ();
	   sleep (1);
	   theRadio	-> stop ();
	   delete theDevice;
	   exit (22);
	}

	std::cerr << "there might be a DAB signal here" << endl;

	while (!ensembleRecognized. load () &&
	                             (--freqSyncTime >= 0)) {
	   std::cerr << freqSyncTime + 1 << "\r";
	   sleep (1);
	}
	std::cerr << "\n";

	if (!ensembleRecognized. load ()) {
	   std::cerr << "no ensemble data found, fatal\n";
	   theDevice -> stopReader ();
	   sleep (1);
	   theRadio	-> stop ();
	   delete theDevice;
	   exit (22);
	}

	run. store (true);
	std::cerr << "we try to start program " <<
	                                         programName << "\n";
	audiodata ad;
	theRadio -> dataforAudioService (programName. c_str (), &ad);
	if (!ad. defined) {
	   std::cerr << "sorry  we cannot handle service " << 
	                                         programName << "\n";
	   run. store (false);
	   theRadio	-> stop ();
	   delete theDevice;
	   exit (22);
	}

	theRadio	-> reset_msc ();
	theRadio	-> set_audioChannel (&ad);
	theDevice	-> startTransmitter (fmFrequency);

	while (run. load () && (theDuration != 0)) {
	   if (theDuration > 0)
	      theDuration --;
	   while (run. load ()) {
	      message m;
	      while (!messageQueue. pop (1000, &m));
	      switch (m. key) {
	         case S_NEW_AUDIO:
	            pcmHandler (std::stoi (m. string));
	            break;

	         case S_NEW_DYNAMICLABEL:
	            handle_dynamicLabel	(m. string);
	            break;

	         case S_QUIT:
	            run. store (false);
	            break;
	      }
	   }
	}
	theRadio	-> stop ();
	theDevice	-> stopReader ();
	theDevice	-> stopTransmitter ();
	delete	theDevice;	
	delete	soundOut;
	delete	streamerOut;
}

void    printOptions (void) {
	std::cerr << 
"                          dab-cmdline options are\n"
"	                  -T Duration\tstop after <Duration> seconds\n"
"	                  -M Mode\tMode is 1, 2 or 4. Default is Mode 1\n"
"	                  -D number\tamount of time to look for an ensemble\n"
"	                  -d number\tseconds to reach time sync\n"
"	                  -P name\tprogram to be selected in the ensemble\n"
"			  -A name\t select the audio channel (portaudio)\n"
"	                  -O fileName\t output to file <name>\n"
"	                  -B Band\tBand is either L_BAND or BAND_III (default)\n"
"	                  -C Channel\n"
"	                  -G Gain in dB (range 0 .. 70)\n"
"	                  -Q autogain (default off)\n"
"	                  -F FM frequency (in KHz)\n";
}

