/*
    MIXERLIB:  An audio mixer library based on the SDL library
    Copyright (C) 1997-1999  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    5635-34 Springhouse Dr.
    Pleasanton, CA 94588 (USA)
    slouken@devolution.com
*/

#include <SDL/SDL_endian.h>
#include <SDL/SDL_audio.h>

#include "mixer.h"

/* The music command hack is UNIX specific */
#ifndef unix
#undef CMD_MUSIC
#endif

#ifdef CMD_MUSIC
#include "music_cmd.h"
#endif
#ifdef WAV_MUSIC
#include "wavestream.h"
#endif
#ifdef MOD_MUSIC
#include "mikmod.h"
#endif
#ifdef MID_MUSIC
#include "timidity.h"
#endif
#ifdef MP3_MUSIC
#include <smpeg/smpeg.h>

static SDL_AudioSpec used_mixer;
#endif

int music_active = 1;
static int music_loops = 0;
static char *music_cmd = NULL;
static int samplesize;
static Mix_Music *music_playing = 0;
static int music_volume;
static int music_swap8;
static int music_swap16;
struct _Mix_Music {
	enum {
		MUS_CMD,
		MUS_WAV,
		MUS_MOD,
		MUS_MID,
		MUS_MP3
	} type;
	union {
#ifdef CMD_MUSIC
		MusicCMD *cmd;
#endif
#ifdef WAV_MUSIC
		WAVStream *wave;
#endif
#ifdef MOD_MUSIC
		UNIMOD *module;
#endif
#ifdef MID_MUSIC
		MidiSong *midi;
#endif
#ifdef MP3_MUSIC
		SMPEG *mp3;
#endif
	} data;
	int error;
};
static int timidity_ok;

/* Mixing function */
void music_mixer(void *udata, Uint8 *stream, int len)
{
	int i;

	if ( music_playing ) {
		switch (music_playing->type) {
#ifdef CMD_MUSIC
			case MUS_CMD:
				/* The playing is done externally */
				break;
#endif
#ifdef WAV_MUSIC
			case MUS_WAV:
				WAVStream_PlaySome(stream, len);
				break;
#endif
#ifdef MOD_MUSIC
			case MUS_MOD:
				VC_WriteBytes((SBYTE *)stream, len);
				if ( music_swap8 ) {
					Uint8 *dst;

					dst = stream;
					for ( i=len; i; --i ) {
						*dst++ ^= 0x80;
					}
				} else
				if ( music_swap16 ) {
					Uint8 *dst, tmp;

					dst = stream;
					for ( i=(len/2); i; --i ) {
						tmp = dst[0];
						dst[0] = dst[1];
						dst[1] = tmp;
						dst += 2;
					}
				}
				break;
#endif
#ifdef MID_MUSIC
			case MUS_MID:
				Timidity_PlaySome(stream, len/samplesize);
				break;
#endif
#ifdef MP3_MUSIC
		    case MUS_MP3:
			    SMPEG_playAudio(music_playing->data.mp3, stream, len);
				break;
#endif
			default:
				/* Unknown music type?? */
				break;
		}
	}
}

/* Initialize the music players with a certain desired audio format */
int open_music(SDL_AudioSpec *mixer)
{
	int music_error;

	music_error = 0;
#ifdef WAV_MUSIC
	if ( WAVStream_Init(mixer) < 0 ) {
		++music_error;
	}
#endif
#ifdef MOD_MUSIC
	/* Set the MikMod music format */
	music_swap8 = 0;
	music_swap16 = 0;
	switch (mixer->format) {

		case AUDIO_U8:
		case AUDIO_S8: {
			if ( mixer->format == AUDIO_S8 ) {
				music_swap8 = 1;
			}
			md_mode = 0;
		}
		break;

		case AUDIO_S16LSB:
		case AUDIO_S16MSB: {
			/* See if we need to correct MikMod mixing */
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
			if ( mixer->format == AUDIO_S16MSB ) {
#else
			if ( mixer->format == AUDIO_S16LSB ) {
#endif
				music_swap16 = 1;
			}
			md_mode = DMODE_16BITS;
		}
		break;

		default: {
			SDL_SetError("Unknown hardware audio format");
			++music_error;
		}
	}
	if ( mixer->channels > 1 ) {
		if ( mixer->channels > 2 ) {
			SDL_SetError("Hardware uses more channels than mixer");
			++music_error;
		}
		md_mode |= DMODE_STEREO;
	}
	samplesize     = mixer->size/mixer->samples;
	md_mixfreq     = mixer->freq;
	md_device      = 0;
	md_volume      = 96;
	md_musicvolume = 128;
	md_sndfxvolume = 128;
	md_pansep      = 128;
	md_reverb      = 0;
	MikMod_RegisterAllLoaders();
	MikMod_RegisterAllDrivers();
	if ( MikMod_Init() ) {
		SDL_SetError("%s", _mm_errmsg[_mm_errno]);
		++music_error;
	}
#endif
#ifdef MID_MUSIC
	if ( Timidity_Init(mixer->freq,
			mixer->format, mixer->channels, mixer->samples) == 0 ) {
		timidity_ok = 1;
	} else {
		timidity_ok = 0;
	}
#endif
#ifdef MP3_MUSIC
	/* Keep a copy of the mixer */
	used_mixer = *mixer;
#endif
	music_playing = 0;
	if ( music_error ) {
		return(-1);
	}
	Mix_VolumeMusic(SDL_MIX_MAXVOLUME);

	return(0);
}

/* Load a music file */
Mix_Music *Mix_LoadMUS(const char *file)
{
	FILE *fp;
	unsigned char magic[5];
	Mix_Music *music;

	/* Figure out what kind of file this is */
	fp = fopen(file, "rb");
	if ( (fp == NULL) || !fread(magic, 4, 1, fp) ) {
		if ( fp != NULL ) {
			fclose(fp);
		}
		SDL_SetError("Couldn't read from '%s'", file);
		return(NULL);
	}
	magic[4] = '\0';
	fclose(fp);

	/* Allocate memory for the music structure */
	music = (Mix_Music *)malloc(sizeof(Mix_Music));
	if ( music == NULL ) {
		SDL_SetError("Out of memory");
		return(NULL);
	}
	music->error = 0;

#ifdef CMD_MUSIC
	if ( music_cmd ) {
		music->type = MUS_CMD;
		music->data.cmd = MusicCMD_LoadSong(music_cmd, file);
		if ( music->data.cmd == NULL ) {
			music->error = 1;
		}
	} else
#endif
#ifdef WAV_MUSIC
	/* WAVE files have the magic four bytes "RIFF"
	   AIFF files have the magic 12 bytes "FORM" XXXX "AIFF"
	 */
	if ( (strcmp(magic, "RIFF") == 0) || (strcmp(magic, "FORM") == 0) ) {
		music->type = MUS_WAV;
		music->data.wave = WAVStream_LoadSong(file, magic);
		if ( music->data.wave == NULL ) {
			music->error = 1;
		}
	} else
#endif
#ifdef MID_MUSIC
	/* MIDI files have the magic four bytes "MThd" */
	if ( strcmp(magic, "MThd") == 0 ) {
		music->type = MUS_MID;
		if ( timidity_ok ) {
			music->data.midi = Timidity_LoadSong((char *)file);
			if ( music->data.midi == NULL ) {
				SDL_SetError("%s", Timidity_Error());
				music->error = 1;
			}
		}
		else {
			SDL_SetError("%s", Timidity_Error());
			music->error = 1;
		}
	} else
#endif
#ifdef MP3_MUSIC
	if ( magic[0]==0xFF && (magic[1]&0xF0)==0xF0) {
		SMPEG_Info info;
		music->type = MUS_MP3;
		music->data.mp3 = SMPEG_new(file, &info, 0);
		if(!info.has_audio){
			SDL_SetError("MPEG file does not have any audio stream.");
			music->error = 1;
		}else{
			SMPEG_actualSpec(music->data.mp3, &used_mixer);
		}
	} else
#endif
#ifdef MOD_MUSIC
	if ( 1 ) {
		music->type = MUS_MOD;
		music->data.module = MikMod_LoadSong((char *)file, 64);
		if ( music->data.module == NULL ) {
			SDL_SetError("%s", _mm_errmsg[_mm_errno]);
			music->error = 1;
		}
	} else
#endif
	{
		SDL_SetError("Unrecognized music format");
		music->error = 1;
	}
	if ( music->error ) {
		free(music);
		music = NULL;
	}
	return(music);
}

/* Free a music chunk previously loaded */
void Mix_FreeMusic(Mix_Music *music)
{
	if ( music ) {
		/* Caution: If music is playing, mixer will crash */
		if ( music == music_playing ) {
			Mix_HaltMusic();
		}
		switch (music->type) {
#ifdef CMD_MUSIC
			case MUS_CMD:
				MusicCMD_FreeSong(music->data.cmd);
				break;
#endif
#ifdef WAV_MUSIC
			case MUS_WAV:
				WAVStream_FreeSong(music->data.wave);
				break;
#endif
#ifdef MOD_MUSIC
			case MUS_MOD:
				MikMod_FreeSong(music->data.module);
				break;
#endif
#ifdef MID_MUSIC
			case MUS_MID:
				Timidity_FreeSong(music->data.midi);
				break;
#endif
#ifdef MP3_MUSIC
		    case MUS_MP3:
				SMPEG_delete(music->data.mp3);
				break;
#endif
			default:
				/* Unknown music type?? */
				break;
		}
		free(music);
	}
}

/* Play a music chunk.  Returns 0, or -1 if there was an error.
*/
int Mix_PlayMusic(Mix_Music *music, int loops)
{
	/* Don't play null pointers :-) */
	if ( music == NULL ) {
		return(-1);
	}
	switch (music->type) {
#ifdef CMD_MUSIC
		case MUS_CMD:
			MusicCMD_SetVolume(music_volume);
			MusicCMD_Start(music->data.cmd);
			break;
#endif
#ifdef WAV_MUSIC
		case MUS_WAV:
			WAVStream_SetVolume(music_volume);
			WAVStream_Start(music->data.wave);
			break;
#endif
#ifdef MOD_MUSIC
		case MUS_MOD:
			Player_SetVolume(music_volume);
			Player_Start(music->data.module);
			Player_SetPosition(0);
			break;
#endif
#ifdef MID_MUSIC
		case MUS_MID:
			Timidity_SetVolume(music_volume);
			Timidity_Start(music->data.midi);
			break;
#endif
#ifdef MP3_MUSIC
	    case MUS_MP3:
			SMPEG_enableaudio(music->data.mp3,1);
			SMPEG_enablevideo(music->data.mp3,0);
			SMPEG_setvolume(music->data.mp3,((float)music_volume/(float)MIX_MAX_VOLUME)*100.0);
			SMPEG_loop(music->data.mp3, loops);
			SMPEG_play(music->data.mp3);
			break;
#endif
		default:
			/* Unknown music type?? */
			return(-1);
	}
	music_active = 1;
	music_playing = music;
	return(0);
}

/* Set the music volume */
int Mix_VolumeMusic(int volume)
{
	int prev_volume;

	prev_volume = music_volume;
	if ( volume >= 0 ) {
		if ( volume > SDL_MIX_MAXVOLUME ) {
			volume = SDL_MIX_MAXVOLUME;
		}
		music_volume = volume;
		if ( music_playing ) {
			switch (music_playing->type) {
#ifdef CMD_MUSIC
				case MUS_CMD:
					MusicCMD_SetVolume(music_volume);
					break;
#endif
#ifdef WAV_MUSIC
				case MUS_WAV:
					WAVStream_SetVolume(music_volume);
					break;
#endif
#ifdef MOD_MUSIC
				case MUS_MOD:
					Player_SetVolume(music_volume);
					break;
#endif
#ifdef MID_MUSIC
				case MUS_MID:
					Timidity_SetVolume(music_volume);
					break;
#endif
#ifdef MP3_MUSIC
			    case MUS_MP3:
					SMPEG_setvolume(music_playing->data.mp3,((float)music_volume/(float)MIX_MAX_VOLUME)*100.0);
					break;
#endif
				default:
					/* Unknown music type?? */
					break;
			}
		}
	}
	return(prev_volume);
}

/* Halt playing of music */
int Mix_HaltMusic(void)
{
	if ( music_playing ) {
		switch (music_playing->type) {
#ifdef CMD_MUSIC
			case MUS_CMD:
				MusicCMD_Stop(music_playing->data.cmd);
				break;
#endif
#ifdef WAV_MUSIC
			case MUS_WAV:
				WAVStream_Stop();
				break;
#endif
#ifdef MOD_MUSIC
			case MUS_MOD:
				Player_Stop();
				break;
#endif
#ifdef MID_MUSIC
			case MUS_MID:
				Timidity_Stop();
				break;
#endif
#ifdef MP3_MUSIC
		    case MUS_MP3:
				SMPEG_stop(music_playing->data.mp3);
				break;
#endif
			default:
				/* Unknown music type?? */
				return(-1);
		}
		music_playing = 0;
	}
	return(0);
}

/* Pause/Resume the music stream */
void Mix_PauseMusic(void)
{
	if ( music_playing ) {
		switch ( music_playing->type ) {
#ifdef CMD_MUSIC
		case MUS_CMD:
			MusicCMD_Pause(music_playing->data.cmd);
			break;
#endif
#ifdef MP3_MUSIC
		case MUS_MP3:
			SMPEG_pause(music_playing->data.mp3);
			break;
#endif
		}
	}
	music_active = 0;
}
void Mix_ResumeMusic(void)
{
	if ( music_playing ) {
		switch ( music_playing->type ) {
#ifdef CMD_MUSIC
		case MUS_CMD:
			MusicCMD_Resume(music_playing->data.cmd);
			break;
#endif
#ifdef MP3_MUSIC
		case MUS_MP3:
			SMPEG_pause(music_playing->data.mp3);
			break;
#endif
		}
	}
	music_active = 1;
}

void Mix_RewindMusic(void)
{
	if ( music_playing ) {
		switch ( music_playing->type ) {
#ifdef MP3_MUSIC
		case MUS_MP3:
			SMPEG_rewind(music_playing->data.mp3);
			break;
#endif
		}
	}
}

/* Check the status of the music */
int Mix_PlayingMusic(void)
{
	if ( music_playing ) {
		switch (music_playing->type) {
#ifdef CMD_MUSIC
			case MUS_CMD:
				if (!MusicCMD_Active(music_playing->data.cmd)) {
					music_playing = 0;
				}
				break;
#endif
#ifdef WAV_MUSIC
			case MUS_WAV:
				if ( ! WAVStream_Active() ) {
					music_playing = 0;
				}
				break;
#endif
#ifdef MOD_MUSIC
			case MUS_MOD:
				if ( ! Player_Active() ) {
					music_playing = 0;
				}
				break;
#endif
#ifdef MID_MUSIC
			case MUS_MID:
				if ( ! Timidity_Active() ) {
					music_playing = 0;
				}
				break;
#endif
#ifdef MP3_MUSIC
			case MUS_MP3:
				if(SMPEG_status(music_playing->data.mp3)!=SMPEG_PLAYING)
					music_playing = 0;
				break;
#endif
		}
	}
	return(music_playing ? 1 : 0);
}

/* Set the external music playback command */
int Mix_SetMusicCMD(const char *command)
{
	Mix_HaltMusic();
	if ( music_cmd ) {
		free(music_cmd);
		music_cmd = NULL;
	}
	if ( command ) {
		music_cmd = (char *)malloc(strlen(command)+1);
		if ( music_cmd == NULL ) {
			return(-1);
		}
		strcpy(music_cmd, command);
	}
	return(0);
}

/* Uninitialize the music players */
void close_music(void)
{
	Mix_HaltMusic();
#ifdef CMD_MUSIC
	Mix_SetMusicCMD(NULL);
#endif
#ifdef MOD_MUSIC
	MikMod_Exit();
#endif
}
