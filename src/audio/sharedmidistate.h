/*
** sharedmidistate.h
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHAREDMIDISTATE_H
#define SHAREDMIDISTATE_H

#include "config.h"
#include "debugwriter.h"
#include "fluid-fun.h"
#include "exception.h"

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#define SYNTH_INIT_COUNT 2
#define SYNTH_SAMPLERATE 44100

struct Synth
{
	fluid_synth_t *synth;
	bool inUse;
};

struct SharedMidiState
{
	bool inited;
	std::vector<Synth> synths;
	const std::string &soundFont;
	fluid_settings_t *flSettings = nullptr;

	SharedMidiState(const Config &conf)
	    : inited(false),
	      soundFont(conf.midi.soundFont)
	{}

	~SharedMidiState()
	{
		/* We might have initialized, but if the consecutive libfluidsynth
		 * load failed, no resources will have been allocated */
		if (!inited || !HAVE_FLUID)
			return;

		for (size_t i = 0; i < synths.size(); ++i)
		{
			assert(!synths[i].inUse);
			fluid.delete_synth(synths[i].synth);
		}

		fluid.delete_settings(flSettings);
	}

	void initIfNeeded(const Config &conf)
	{
		if (inited)
			return;

		inited = true;

		initFluidFunctions();

		if (!HAVE_FLUID)
			return;

		flSettings = fluid.new_settings();
#ifdef MKXPZ_VITA_FLUIDLITE
        if (!flSettings) {
            inited = false;
            throw Exception(Exception::MKXPError, "Midi: Could not allocate synthesizer settings");
        }
        fluid.settings_setint(flSettings, "synth.polyphony", 64);
#endif
		fluid.settings_setnum(flSettings, "synth.gain", 1.0f);
		fluid.settings_setnum(flSettings, "synth.sample-rate", SYNTH_SAMPLERATE);
		fluid.settings_setint(flSettings, "synth.chorus.active", conf.midi.chorus);
		fluid.settings_setint(flSettings, "synth.reverb.active", conf.midi.reverb);

#ifndef MKXPZ_VITA_FLUIDLITE
		for (size_t i = 0; i < SYNTH_INIT_COUNT; ++i)
			addSynth(false);
#endif
	}

	fluid_synth_t *allocateSynth()
	{
		assert(HAVE_FLUID);
		assert(inited);

		size_t i;

		for (i = 0; i < synths.size(); ++i)
			if (!synths[i].inUse)
				break;

		if (i < synths.size())
		{
			fluid_synth_t *syn = synths[i].synth;
			fluid.synth_system_reset(syn);
			synths[i].inUse = true;

			return syn;
		}
		else
		{
			return addSynth(true);
		}
	}

	void releaseSynth(fluid_synth_t *synth)
	{
		size_t i;

		for (i = 0; i < synths.size(); ++i)
			if (synths[i].synth == synth)
				break;

		assert(i < synths.size());

		synths[i].inUse = false;
	}

private:
	fluid_synth_t *addSynth(bool usedNow)
	{
#ifdef MKXPZ_VITA_FLUIDLITE
        // Validate the file before entering the loader, including missing paths.
        // A bounded SF2 bank keeps sample allocation appropriate for the Vita.
        unsigned char header[12]{};
        std::FILE *file = std::fopen(soundFont.c_str(), "rb");
        bool valid = false;
        if (file) {
            bool headerRead = std::fread(header, 1, sizeof(header), file) == sizeof(header);
            bool end = std::fseek(file, 0, SEEK_END) == 0;
            long size = end ? std::ftell(file) : -1;
            uint32_t riffSize = uint32_t(header[4]) | (uint32_t(header[5]) << 8) |
                                (uint32_t(header[6]) << 16) | (uint32_t(header[7]) << 24);
            valid = headerRead && size >= 12 && size <= 32*1024*1024 &&
                    !std::memcmp(header, "RIFF", 4) && !std::memcmp(header+8, "sfbk", 4) &&
                    uint64_t(riffSize)+8 == uint64_t(size);
            std::fclose(file);
        }
        if (!valid)
            throw Exception(Exception::MKXPError, "Midi: Invalid or missing SF2 bank '%s' (maximum 32 MiB). Set midiSoundFont to a valid SF2 file.", soundFont.c_str());
#endif
		fluid_synth_t *syn = fluid.new_synth(flSettings);

#ifdef MKXPZ_VITA_FLUIDLITE
        // MIDI synthesis and sample memory are allocated only for a MIDI source.
        if (!syn)
            throw Exception(Exception::MKXPError, "Midi: Could not allocate synthesizer");
        if (soundFont.empty() || fluid.synth_sfload(syn, soundFont.c_str(), 1) < 0) {
            fluid.delete_synth(syn);
            throw Exception(Exception::MKXPError, "Midi: Could not load SoundFont '%s'. Set midiSoundFont to a valid SF2 file.", soundFont.c_str());
        }
#else
		if (!soundFont.empty())
			fluid.synth_sfload(syn, soundFont.c_str(), 1);
		else
			Debug() << "Warning: No soundfont specified, sound might be mute";
#endif

		Synth synth;
		synth.inUse = usedNow;
		synth.synth = syn;
        try {synths.push_back(synth);}
        catch (...) {fluid.delete_synth(syn);throw;}

		return syn;
	}
};

#endif // SHAREDMIDISTATE_H
