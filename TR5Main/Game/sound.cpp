#include "sound.h"

HSTREAM BASS_3D_Mixdown;
HFX BASS_FXHandler[NUM_SOUND_FILTERS];
BASS_BFX_FREEVERB BASS_ReverbTypes[NUM_REVERB_TYPES]; // Reverb presets
SoundTrackSlot BASS_Soundtrack[NUM_SOUND_TRACK_TYPES];

bool __cdecl Sound_LoadSample(char *pointer, __int32 compSize, __int32 uncompSize, __int32 index)	// Replaces DXCreateSampleADPCM()
{
	printf("Loading sample %d  \n", index);

	if (BASS_GetDevice() == -1)
	{
		printf("No valid BASS devices found. \n");
		return 0;
	}

	if (index >= SOUND_MAX_SAMPLES)
	{
		printf("Sample index is larger than max. amount of samples (%d) \n", index);
		return 0;
	}

	if (pointer == NULL || compSize <= 0)
	{
		printf("Sample size or memory address is incorrect \n", index);
		return 0;
	}

	// Load and uncompress sample to 32-bit float format
	HSAMPLE sample = BASS_SampleLoad(true, pointer, 0, compSize, 1, SOUND_SAMPLE_FLAGS);

	if (!sample)
	{
		printf("Error loading sample %d \n", index);
		return false;
	}

	// Paranoid (c) TeslaRus
	// Try to free sample before allocating new one
	Sound_FreeSample(index);

	BASS_SAMPLE info;
	BASS_SampleGetInfo(sample, &info);
	int finalLength = info.length + 44;	// uncompSize is invalid after 16->32 bit conversion

	if (info.freq != 22050 || info.chans != 1)
	{
		printf("Wrong sample parameters, must be 22050 Hz Mono \n");
		return false;
	}

	// Generate RIFF/WAV header to simplify loading sample data to stream. In case if RIFF/WAV header
	// exists, stream could be completely created just by calling BASS_StreamCreateFile().
	char* uncompBuffer = new char[finalLength];
	ZeroMemory(uncompBuffer, finalLength);
	memcpy(uncompBuffer, "RIFF\0\0\0\0WAVEfmt \20\0\0\0", 20);
	memcpy(uncompBuffer + 36, "data\0\0\0\0", 8);

	WAVEFORMATEX *wf = (WAVEFORMATEX*)(uncompBuffer + 20);

	wf->wFormatTag = 3;
	wf->nChannels = info.chans;
	wf->wBitsPerSample = 32;
	wf->nSamplesPerSec = info.freq;
	wf->nBlockAlign = wf->nChannels * wf->wBitsPerSample / 8;
	wf->nAvgBytesPerSec = wf->nSamplesPerSec * wf->nBlockAlign;

	// Copy raw PCM data from temporary sample buffer to actual buffer which will be used by engine.
	BASS_SampleGetData(sample, uncompBuffer + 44);
	BASS_SampleFree(sample);

	// Cut off trailing silence from samples to prevent gaps in looped playback
	int cleanLength = info.length;
	for (int i = 4; i < info.length; i += 4)
	{
		float *currentSample = reinterpret_cast<float*>(uncompBuffer + finalLength - i);
		if (*currentSample > SOUND_32BIT_SILENCE_LEVEL || *currentSample < -SOUND_32BIT_SILENCE_LEVEL)
		{
			int alignment = i % wf->nBlockAlign;
			cleanLength -= (i - alignment);
			break;
		}
	}

	// Put data size to header
	*(DWORD*)(uncompBuffer + 4) = cleanLength + 44 - 8;
	*(DWORD*)(uncompBuffer + 40) = cleanLength;

	// Create actual sample
	SamplePointer[index] = BASS_SampleLoad(true, uncompBuffer, 0, cleanLength + 44, 65535, SOUND_SAMPLE_FLAGS | BASS_SAMPLE_3D);
	printf("Sample loaded into buffer, size %d \n", info.length);
	return true;
}

long __cdecl SoundEffect(__int32 effectID, PHD_3DPOS* position, __int32 env_flags)
{
	if (effectID >= SOUND_LEGACY_SOUNDMAP_SIZE)
		return 0;

	if (BASS_GetDevice() == -1)
		return 0;

	if (!(env_flags & ENV_FLAG_SFX_ALWAYS))
	{
		// Don't play effect if effect's environment isn't the same as camera position's environment
		// @TODO: Later redo with proper EQ damping to be able to subtly hear sounds from underwater!
		if ((env_flags & ENV_FLAG_WATER) != (Rooms[Camera.pos.roomNumber].flags & ENV_FLAG_WATER))
			return 0;
	}

	// Get actual sample index from SoundMap
	int sampleIndex = SampleLUT[effectID];

	// -1 means no such effect exists in level file.
	// We set it to -2 afterwards to prevent further debug message firings.
	if (sampleIndex == -1)
	{
		printf("Non present effect %d \n", effectID);
		SampleLUT[effectID] = -2;
		return 0;
	}
	else if (sampleIndex == -2)
		return 0;

	SAMPLE_INFO *sampleInfo = &SampleInfo[sampleIndex];

	if (sampleInfo->number < 0)
	{
		printf("No valid samples count for effect %d", sampleIndex);
		return 0;
	}

	// Assign common sample flags.
	DWORD sampleFlags = SOUND_SAMPLE_FLAGS;

	// Effect's chance to play.
	if ((sampleInfo->randomness) && ((GetRandomControl() & 127) > sampleInfo->randomness))
		return 0;

	// Apply 3D attrib only to sound with position property
	if (position)
		sampleFlags |= BASS_SAMPLE_3D;

	// Select behaviour based on effect playback type (bytes 0-1 of flags field)
	int playType = sampleInfo->flags & 3;
	switch (playType)
	{
	case SOUND_NORMAL:
		break;

	case SOUND_WAIT:
		if (Sound_EffectIsPlaying(effectID, position) != -1) return 0; // Don't play until stopped
		break;

	case SOUND_RESTART:
		Sound_FreeSlot(Sound_EffectIsPlaying(effectID, position), 100);	// Stop existing and continue
		break;

	case SOUND_LOOPED:
		if (Sound_UpdateEffectPosition(Sound_EffectIsPlaying(effectID, position), position))
			return 0;
		sampleFlags |= BASS_SAMPLE_LOOP;
		break;
	}

	float radius   = (float)(sampleInfo->radius) * 1024.0f;
	float distance = Sound_DistanceToListener(position);

	// Don't play sound if it's too far from listener's position.
	if (distance > radius)
		return 0;

	// Set and randomize volume (if needed)
	float gain = static_cast<float>(sampleInfo->volume) / 255.0f;
	if ((sampleInfo->flags & SOUND_FLAG_RND_GAIN))
		gain -= (static_cast<float>(GetRandomControl()) / static_cast<float>(RAND_MAX)) * SOUND_MAX_GAIN_CHANGE;

	// Set and randomize pitch (if needed)
	float pitch = 1.0f + static_cast<float>(sampleInfo->pitch) / 127.0f;
	if ((sampleInfo->flags & SOUND_FLAG_RND_PITCH))
		pitch += ((static_cast<float>(GetRandomControl()) / static_cast<float>(RAND_MAX)) - 0.5f) * SOUND_MAX_PITCH_CHANGE * 2.0f;

	// Randomly select arbitrary sample from the list, if more than one is present
	int sampleToPlay = 0;
	int numSamples = (sampleInfo->flags >> 2) & 15;
	if (numSamples == 1)
		sampleToPlay = sampleInfo->number;
	else
		sampleToPlay = sampleInfo->number + (int)((GetRandomControl() * numSamples) >> 15);

	// Get free channel to play sample
	int freeSlot = Sound_GetFreeSlot();
	if (freeSlot == -1)
	{
		printf("No free channel slot available!");
		return 0;
	}

	// We need to set stream buffer to minimum before assigning channel
	// It's okay to use such small buffer, because whole processing happens in RAM.
	BASS_SetConfig(BASS_CONFIG_BUFFER, 10);

	HSAMPLE handle = SamplePointer[sampleToPlay];

	// Create sample's stream and reset buffer back to normal value.
	HSTREAM channel = BASS_SampleGetChannel(handle, true);
	BASS_SetConfig(BASS_CONFIG_BUFFER, 300);

	if (Sound_CheckBASSError("Trying to create channel for sample %d", sampleToPlay))
		return 0;

	// Set pitch/volume settings appropriately
	BASS_ChannelSetAttribute(channel, BASS_ATTRIB_FREQ, 22050.0f * pitch);
	BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, Sound_Attenuate(gain, distance, radius));

	if (Sound_CheckBASSError("Applying pitch/gain attribs on channel %x, sample %d", channel, sampleToPlay))
		return 0;

	// Finally ready to play sound, assign it to sound slot.
	SoundSlot[freeSlot].state = SOUND_STATE_IDLE;
	SoundSlot[freeSlot].effectID = effectID;
	SoundSlot[freeSlot].channel = channel;
	SoundSlot[freeSlot].gain = gain;
	SoundSlot[freeSlot].origin = position ? D3DXVECTOR3(position->xPos, position->yPos, position->zPos) : D3DXVECTOR3(0, 0, 0);

	// Set 3D attributes
	if (position)
	{
		BASS_ChannelSet3DAttributes(channel, position ? BASS_3DMODE_NORMAL : BASS_3DMODE_OFF, SOUND_MAXVOL_RADIUS, radius, 360, 360, 0.0f);
		Sound_UpdateEffectPosition(freeSlot, position);

		if (Sound_CheckBASSError("Applying 3D attribs on channel %x, sound %d", channel, effectID))
			return 0;
	}

	// Set looped flag, if necessary
	if(playType == SOUND_LOOPED)
		BASS_ChannelFlags(channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);

	// Play channel
	BASS_ChannelPlay(channel, false);

	if (Sound_CheckBASSError("Queuing channel %x on sample mixer", freeSlot))
		return 0;

	return 1;
}

void __cdecl StopSoundEffect(__int16 effectID)
{
	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
		if (SoundSlot[i].effectID == effectID && SoundSlot[i].channel != 0 && BASS_ChannelIsActive(SoundSlot[i].channel) == BASS_ACTIVE_PLAYING)
		{
			BASS_ChannelStop(SoundSlot[i].channel);
			SoundSlot[i].channel = NULL;
			SoundSlot[i].effectID = -1;
		}
}

void __cdecl SOUND_Stop()
{
	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
		if (SoundSlot[i].channel != 0 && BASS_ChannelIsActive(SoundSlot[i].channel))
			BASS_ChannelStop(SoundSlot[i].channel);
	ZeroMemory(SoundSlot, (sizeof(SoundEffectSlot) * SOUND_MAX_CHANNELS));
}

void __cdecl Sound_FreeSamples()
{
	SOUND_Stop();
	for (int i = 0; i < SOUND_MAX_SAMPLES; i++)
		Sound_FreeSample(i);
}

void __cdecl S_CDPlay(short index, unsigned int mode)
{
	bool  crossfade = false;
	DWORD crossfadeTime;
	DWORD flags = BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT | BASS_ASYNCFILE;

	if (index >= SOUND_LEGACY_TRACKTABLE_SIZE || index < 0)
		return;

	mode = (mode >= NUM_SOUND_TRACK_TYPES) ? SOUND_TRACK_BACKGROUND : mode;

	if (BASS_ChannelIsActive(BASS_Soundtrack[mode].channel))
	{
		if (BASS_Soundtrack[mode].trackID == index)
			return;
	}

	switch (mode)
	{
		case SOUND_TRACK_ONESHOT:
			crossfadeTime = 200;
			break;

		case SOUND_TRACK_BACKGROUND:
			crossfade = true;
			crossfadeTime = 5000;
			flags |= BASS_SAMPLE_LOOP;
			break;
	}

	BASS_ChannelSlideAttribute(BASS_Soundtrack[mode].channel, BASS_ATTRIB_VOL, -1.0f, crossfadeTime);
	
	static char fullTrackName[1024];

	char* mask = &TrackNamePrefix;
	char* name = TrackNameTable[index];

	snprintf(fullTrackName, sizeof(fullTrackName), &TrackNamePrefix, TrackNameTable[index]);

	auto stream = BASS_StreamCreateFile(false, fullTrackName, 0, 0, flags);

	BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, 0.6f); // @TODO: Patch volume into original settings!
	BASS_ChannelPlay(stream, false);

	// BGM tracks are crossfaded, and additionally shuffled a bit to make things more natural.
	// Think everybody are fed up with same start-up sounds of Caves ambience...

	if (crossfade)
	{		
		// Crossfade...
		BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, 0.0f);
		BASS_ChannelSlideAttribute(stream, BASS_ATTRIB_VOL, 0.6f, crossfadeTime);// @TODO: Patch volume into original settings!

		// Shuffle...
		QWORD newPos = BASS_ChannelGetLength(stream, BASS_POS_BYTE) * (static_cast<float>(GetRandomControl()) / static_cast<float>(RAND_MAX));
		BASS_ChannelSetPosition(stream, newPos, BASS_POS_BYTE);
	}

	BASS_Soundtrack[mode].channel = stream;
	BASS_Soundtrack[mode].trackID = index;
}

void __cdecl S_CDPlayEx(short index, DWORD mask, DWORD unknown)
{
	static short loopedTracks[] = { 117, 118, 121, 123, 124, 125, 126, 127, 128, 129, 130 };
	bool looped = false;

	if (index >= SOUND_LEGACY_TRACKTABLE_SIZE || index < 0)
		return;

	// Assign looping based on hardcoded track IDs.
	// @TODO: Replace with scripted ones (or trigger property) later.
	int size = sizeof(loopedTracks) / sizeof(short);
	for (int i = 0; i < size; i++)
		if (index == loopedTracks[i])
		{
			looped = true;
			break;
		}

	// Check and modify soundtrack map mask, if needed.
	// If existing mask is unmodified (same activation mask setup), track won't play.
	if (!looped)
	{
		byte filteredMask = (mask >> 8) & 0x3F;
		if ((TrackMap[index] & filteredMask) == filteredMask)
			return;	// Mask is the same, don't play it.

		TrackMap[index] |= filteredMask;
	}

	S_CDPlay(index, looped);
}

void __cdecl S_CDStop()
{
	// Do quick fadeouts.
	BASS_ChannelSlideAttribute(BASS_Soundtrack[SOUND_TRACK_ONESHOT].channel, BASS_ATTRIB_VOL | BASS_SLIDE_LOG, -1.0f, 200);
	BASS_ChannelSlideAttribute(BASS_Soundtrack[SOUND_TRACK_BACKGROUND].channel, BASS_ATTRIB_VOL | BASS_SLIDE_LOG, -1.0f, 200);

	BASS_Soundtrack[SOUND_TRACK_ONESHOT].trackID = -1;
	BASS_Soundtrack[SOUND_TRACK_BACKGROUND].trackID = -1;
}

void Sound_FreeSample(__int32 index)
{
	if (SamplePointer[index] != NULL)
	{
		BASS_SampleFree(SamplePointer[index]);
		SamplePointer[index] = NULL;
	}
}

// Get first free (non-playing) sound slot.
// If no free slots found, now try to hijack slot which is as far from listener as possible

int Sound_GetFreeSlot()
{
	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
	{
		if ((SoundSlot[i].channel == NULL) ||
			!BASS_ChannelIsActive(SoundSlot[i].channel))
			return i;
	}

	// No free slots, hijack now.

	float minDistance = 0;
	int farSlot = -1;

	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
	{
		float distance = D3DXVec3Length(&D3DXVECTOR3(SoundSlot[i].origin - D3DXVECTOR3(Camera.mikePos.x,
																					   Camera.mikePos.y,
																					   Camera.mikePos.z)));
		if (distance > minDistance)
		{
			minDistance = distance;
			farSlot = i;
		}
	}

	printf("Hijacked sound effect slot %d", farSlot);
	return farSlot;
}

// Returns slot ID in which effect is playing, if found. If not found, returns -1.
// We use origin position as a reference, because in original TRs it's not possible to clearly
// identify what's the source of the producing effect.

int Sound_EffectIsPlaying(int effectID, PHD_3DPOS *position)
{
	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
	{
		if (SoundSlot[i].effectID == effectID)
		{
			if (SoundSlot[i].channel == NULL)	// Free channel
				continue;

			if (BASS_ChannelIsActive(SoundSlot[i].channel))
			{
				// Only check position on 3D samples. 2D samples stop immediately.

				BASS_CHANNELINFO info;
				BASS_ChannelGetInfo(SoundSlot[i].channel, &info);
				if (!(info.flags & BASS_SAMPLE_3D) || !position)
					return i;

				// Check if effect origin is equal OR in nearest possible hearing range.

				D3DXVECTOR3 origin = D3DXVECTOR3(position->xPos, position->yPos, position->zPos);
				if (SoundSlot[i].origin == origin ||
					D3DXVec3Length(&D3DXVECTOR3(SoundSlot[i].origin - origin)) < SOUND_MAXVOL_RADIUS)
					return i;
			}
			else
				SoundSlot[i].channel = NULL; // WTF, let's clean this up
		}
	}
	return -1;
}

// Gets the distance to the source.

float Sound_DistanceToListener(PHD_3DPOS *position)
{
	if (!position) return 0.0f;	// Assume sound is 2D menu sound
	return Sound_DistanceToListener(D3DXVECTOR3(position->xPos, position->yPos, position->zPos));
}
float Sound_DistanceToListener(D3DXVECTOR3 position)
{
	return D3DXVec3Length(&D3DXVECTOR3(D3DXVECTOR3(Camera.mikePos.x, Camera.mikePos.y, Camera.mikePos.z) - position));
}

// Calculate attenuated volume.

float Sound_Attenuate(float gain, float distance, float radius)
{
	float result = gain * (1.0f - (distance / radius));
	result = result < 0 ? 0.0f : (result > 1.0f ? 1.0f : result);
	return result;
}

// Stop and free desired sound slot.

void Sound_FreeSlot(int index, unsigned int fadeout = 0)
{
	if (index > SOUND_MAX_CHANNELS || index < 0)
		return;

	if (fadeout > 0)
		BASS_ChannelSlideAttribute(SoundSlot[index].channel, BASS_ATTRIB_VOL, -1.0f, fadeout);
	else
		BASS_ChannelStop(SoundSlot[index].channel);

	SoundSlot[index].channel = NULL;
	SoundSlot[index].state = SOUND_STATE_IDLE;
}

// Update sound position in a level.

bool Sound_UpdateEffectPosition(int index, PHD_3DPOS *position)
{
	if (index > SOUND_MAX_CHANNELS || index < 0)
		return false;

	if (position)
	{
		BASS_CHANNELINFO info;
		BASS_ChannelGetInfo(SoundSlot[index].channel, &info);
		if (info.flags & BASS_SAMPLE_3D)
		{
			BASS_ChannelSet3DPosition(SoundSlot[index].channel, &BASS_3DVECTOR(position->xPos, position->yPos, position->zPos),
				&BASS_3DVECTOR(position->xRot, position->yRot, position->zRot), NULL);
			BASS_Apply3D();
		}
	}

	// Reset activity flag, important for looped samples
	if (BASS_ChannelIsActive(SoundSlot[index].channel))
		SoundSlot[index].state = SOUND_STATE_IDLE;

	return true;
}

// Update whole sound scene in a level.
// Must be called every frame to update camera position and 3D parameters.

void Sound_UpdateScene()
{
	// Apply environmental effects

	static int currentReverb = -1;

	if (currentReverb == -1 || Rooms[Camera.pos.roomNumber].reverbType != currentReverb)
	{
		currentReverb = Rooms[Camera.pos.roomNumber].reverbType;
		if (currentReverb < NUM_REVERB_TYPES)
			BASS_FXSetParameters(BASS_FXHandler[SOUND_FILTER_REVERB], &BASS_ReverbTypes[currentReverb]);
	}

	for (int i = 0; i < SOUND_MAX_CHANNELS; i++)
	{
		if ((SoundSlot[i].channel != 0) && (BASS_ChannelIsActive(SoundSlot[i].channel) == BASS_ACTIVE_PLAYING))
		{
			// Stop and clean up sounds which were in ending state in previous frame

			if (SoundSlot[i].state == SOUND_STATE_ENDING)
			{
				SoundSlot[i].state = SOUND_STATE_ENDED;
				Sound_FreeSlot(i, 100);
				continue;
			}

			// Calculate attenuation and clean up sounds which are out of listener range

			SAMPLE_INFO *sampleInfo = &SampleInfo[SampleLUT[SoundSlot[i].effectID]];

			float radius = (float)(sampleInfo->radius) * 1024.0f;
			float distance = Sound_DistanceToListener(SoundSlot[i].origin);

			if (distance > radius)
			{
				Sound_FreeSlot(i);
				continue;
			}
			else
				BASS_ChannelSetAttribute(SoundSlot[i].channel, BASS_ATTRIB_VOL, Sound_Attenuate(SoundSlot[i].gain, distance, radius));

			// Switch looped samples to ending state to stop them in case they aren't re-fired in next frame

			if (sampleInfo->flags & 3 == SOUND_LOOPED)
				SoundSlot[i].state = SOUND_STATE_ENDING;
		}
	}

	// Apply current listener position

	D3DXVECTOR3 at;
	D3DXVec3Normalize(&at, &(D3DXVECTOR3(Camera.target.x, Camera.target.y, Camera.target.z) -
							 D3DXVECTOR3(Camera.mikePos.x, Camera.mikePos.y, Camera.mikePos.z)));

	BASS_Set3DPosition(&BASS_3DVECTOR(Camera.mikePos.x,		// Pos
									  Camera.mikePos.y,
									  Camera.mikePos.z),
					   &BASS_3DVECTOR(Lara.currentXvel,		// Vel
									  Lara.currentYvel,
									  Lara.currentZvel),
					   &BASS_3DVECTOR(at.x, at.y, at.z),	// At
					   &BASS_3DVECTOR(0.0f, 1.0f, 0.0f));	// Up
	BASS_Apply3D();
}

// Initialize BASS engine and also prepare all sound data.
// Called once on engine start-up.

void Sound_Init()
{
	BASS_Init(-1, 44100, BASS_DEVICE_3D, WindowsHandle, NULL);

	if (Sound_CheckBASSError("Initializing BASS sound device"))
		return;

	// Set 3D world parameters.
	// Rolloff is lessened since we have own attenuation implementation.
	BASS_Set3DFactors(SOUND_BASS_UNITS, 1.5f, 0.5f);	
	BASS_SetConfig(BASS_CONFIG_3DALGORITHM, BASS_3DALG_FULL);

	// Set minimum latency and 2 threads for updating.
	// Most of modern PCs already have multi-core CPUs, so why not parallelize updating?
	BASS_SetConfig(BASS_CONFIG_UPDATETHREADS, 2);
	BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 10);

	// Create 3D mixdown channel and make it play forever.
	// For realtime mixer channels, we need minimum buffer latency. It shouldn't affect reliability.
	BASS_SetConfig(BASS_CONFIG_BUFFER, 40);
	BASS_3D_Mixdown = BASS_StreamCreate(44100, 2, BASS_SAMPLE_FLOAT, STREAMPROC_DEVICE_3D, NULL);
	BASS_ChannelPlay(BASS_3D_Mixdown, false);

	// Reset buffer back to normal value.
	BASS_SetConfig(BASS_CONFIG_BUFFER, 300);

	if (Sound_CheckBASSError("Starting 3D mixdown"))
		return;

	// Initialize channels and tracks array
	ZeroMemory(BASS_Soundtrack, (sizeof(HSTREAM) * NUM_SOUND_TRACK_TYPES));
	ZeroMemory(SoundSlot, (sizeof(SoundEffectSlot) * SOUND_MAX_CHANNELS));

	// Set up reverb presets
	
	BASS_ReverbTypes[RVB_OUTSIDE].fDryMix = 1.0f;
	BASS_ReverbTypes[RVB_OUTSIDE].fWetMix = 0.2f;
	BASS_ReverbTypes[RVB_OUTSIDE].fWidth = 1.0f;
	BASS_ReverbTypes[RVB_OUTSIDE].fRoomSize = 0.05f;
	BASS_ReverbTypes[RVB_OUTSIDE].fDamp = 0.65f;
	BASS_ReverbTypes[RVB_OUTSIDE].lChannel = -1;

	BASS_ReverbTypes[RVB_SMALL_ROOM].fDryMix = 1.0f;
	BASS_ReverbTypes[RVB_SMALL_ROOM].fWetMix = 0.2f;
	BASS_ReverbTypes[RVB_SMALL_ROOM].fWidth = 0.8f;
	BASS_ReverbTypes[RVB_SMALL_ROOM].fRoomSize = 0.3f;
	BASS_ReverbTypes[RVB_SMALL_ROOM].fDamp = 0.15f;
	BASS_ReverbTypes[RVB_SMALL_ROOM].lChannel = -1;

	BASS_ReverbTypes[RVB_MEDIUM_ROOM].fDryMix = 1.0f;
	BASS_ReverbTypes[RVB_MEDIUM_ROOM].fWetMix = 0.2f;
	BASS_ReverbTypes[RVB_MEDIUM_ROOM].fWidth = 1.0f;
	BASS_ReverbTypes[RVB_MEDIUM_ROOM].fRoomSize = 0.55f;
	BASS_ReverbTypes[RVB_MEDIUM_ROOM].fDamp = 0.20f;
	BASS_ReverbTypes[RVB_MEDIUM_ROOM].lChannel = -1;

	BASS_ReverbTypes[RVB_LARGE_ROOM].fDryMix = 1.0f;
	BASS_ReverbTypes[RVB_LARGE_ROOM].fWetMix = 0.2f;
	BASS_ReverbTypes[RVB_LARGE_ROOM].fWidth = 1.0f;
	BASS_ReverbTypes[RVB_LARGE_ROOM].fRoomSize = 0.75f;
	BASS_ReverbTypes[RVB_LARGE_ROOM].fDamp = 0.50f;
	BASS_ReverbTypes[RVB_LARGE_ROOM].lChannel = -1;

	BASS_ReverbTypes[RVB_PIPE].fDryMix = 1.0f;
	BASS_ReverbTypes[RVB_PIPE].fWetMix = 0.2f;
	BASS_ReverbTypes[RVB_PIPE].fWidth = 1.0f;
	BASS_ReverbTypes[RVB_PIPE].fRoomSize = 0.9f;
	BASS_ReverbTypes[RVB_PIPE].fDamp = 1.0f;
	BASS_ReverbTypes[RVB_PIPE].lChannel = -1;	

	// Initialize BASS_FX plugin
	BASS_FX_GetVersion();

	// Attach reverb effect to 3D channel
 	BASS_FXHandler[SOUND_FILTER_REVERB] = BASS_ChannelSetFX(BASS_3D_Mixdown, BASS_FX_BFX_FREEVERB, 0);
	BASS_FXSetParameters(BASS_FXHandler[SOUND_FILTER_REVERB], &BASS_ReverbTypes[RVB_OUTSIDE]);

	// Apply slight compression to 3D channel
	BASS_FXHandler[SOUND_FILTER_COMPRESSOR] = BASS_ChannelSetFX(BASS_3D_Mixdown, BASS_FX_BFX_COMPRESSOR2, 1);
	auto comp = BASS_BFX_COMPRESSOR2{ 4.0f, -18.0f, 2.5f, 10.0f, 100.0f, -1 };
	BASS_FXSetParameters(BASS_FXHandler[SOUND_FILTER_COMPRESSOR], &comp);

	return;
}

// Stop all sounds and streams, if any, unplug all channels from the mixer and unload BASS engine.
// Must be called on engine quit.

void Sound_DeInit()
{
	BASS_Free();
}

bool Sound_CheckBASSError(char* message, ...)
{
	va_list argptr;
	static char data[4096];

	int bassError = BASS_ErrorGetCode();
	if (bassError != 0)
	{
		va_start(argptr, message);
		int32_t written = vsprintf(data, message, argptr);	// @TODO: replace with debug/console/message output later...
		va_end(argptr);

		snprintf(data + written, sizeof(data) - written, ": error #%d \n", bassError);
		printf(data);
	}
	return bassError != 0;
}

void Inject_Sound()
{
	INJECT(0x00479060, SOUND_Stop);
	INJECT(0x004790A0, SOUND_Stop);			// SOUND_Init, seems no difference from SOUND_Stop
	INJECT(0x00478FE0, StopSoundEffect);
	INJECT(0x00478570, SoundEffect);
	INJECT(0x004A3510, Sound_LoadSample);	// DXCreateSampleADPCM
	INJECT(0x004A3AA0, Sound_FreeSamples);	// ReleaseDXSoundBuffers

	INJECT(0x00492990, S_CDPlay);			// Not really S_CDPlay, but parent singleton function
	INJECT(0x00418BC0, S_CDPlayEx);			// "S_CDPlayEx" called by CD trigger events
	INJECT(0x004929E0, S_CDStop);			// S_CDStop

	//INJECT(0x004A3100, EmptySoundProc);	// DXDSCreate
	//INJECT(0x004A3190, EmptySoundProc);	// DXCreateSample
	//INJECT(0x004A3030, EmptySoundProc);	// DXSetOutputFormat
	//INJECT(0x004A2E30, EmptySoundProc);	// SetSoundOutputFormat
	//INJECT(0x004A3300, EmptySoundProc);	// StreamOpen
	//INJECT(0x004A3470, EmptySoundProc);	// StreamClose
	//INJECT(0x004931A0, EmptySoundProc);	// ACMClose
	//INJECT(0x00493490, EmptySoundProc);	// ACMStream
	//INJECT(0x00492DA0, EmptySoundProc);	// ACMInit
	//INJECT(0x00492C20, EmptySoundProc);	// SetupNotifications
	//INJECT(0x00493990, EmptySoundProc);	// StartAddress
	//INJECT(0x00492B60, EmptySoundProc);	// fnCallback
}