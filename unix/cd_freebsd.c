/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
/* Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All */
/* rights reserved. */

#if defined(CDAUDIO)
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/cdio.h>

#include "../client/client.h"

#define CDROM_DATA_TRACK 4
#define CDROMPLAYTRKIND 0x5304

static qboolean	cdValid = false;
static qboolean	playing = false;
static qboolean	wasPlaying = false;
static qboolean	initialized = false;
static qboolean	enabled = true;
static qboolean	playLooping = false;
static float	cdvolume;
static byte	remap[100];
static byte	playTrack;
static byte	maxTrack;

static int	cdfile = -1;

/* static char cd_dev[64] = "/dev/cdrom"; */

cvar_t         *cd_volume;
cvar_t         *cd_nocd;
cvar_t         *cd_dev;

void		CDAudio_Pause(void);

static void
CDAudio_Eject(void)
{
	if (cdfile == -1 || !enabled)
		return;		/* no cd init'd */

	if (ioctl(cdfile, CDIOCEJECT) == -1)
		Com_DPrintf("ioctl cdioeject failed\n");
}


static void
CDAudio_CloseDoor(void)
{
	if (cdfile == -1 || !enabled)
		return;		/* no cd init'd */

	if (ioctl(cdfile, CDIOCCLOSE) == -1)
		Com_DPrintf("ioctl cdiocclose failed\n");
}

static int
CDAudio_GetAudioDiskInfo(void)
{
	struct ioc_toc_header tochdr;

	cdValid = false;

	if (ioctl(cdfile, CDIOREADTOCHEADER, &tochdr) == -1) {
		Com_DPrintf("ioctl cdioreadtocheader failed\n");
		return -1;
	}
	if (tochdr.starting_track < 1) {
		Com_DPrintf("CDAudio: no music tracks\n");
		return -1;
	}
	cdValid = true;
	maxTrack = tochdr.ending_track;

	return 0;
}


void
CDAudio_Play(int track, qboolean looping)
{

	struct ioc_read_toc_entry entry;
	struct cd_toc_entry toc_buffer;
	struct ioc_play_track ti;


	if (cdfile == -1 || !enabled)
		return;

	if (!cdValid) {
		CDAudio_GetAudioDiskInfo();
		if (!cdValid)
			return;
	}
	track = remap[track];

	if (track < 1 || track > maxTrack) {
		Com_DPrintf("CDAudio: Bad track number %u.\n", track);
		return;
	}
	bzero((char *)&toc_buffer, sizeof(toc_buffer));
	entry.data_len = sizeof(toc_buffer);
	entry.data = &toc_buffer;
	/* don't try to play a non-audio track */
	entry.starting_track = track;
	entry.address_format = CD_MSF_FORMAT;
	if (ioctl(cdfile, CDIOREADTOCENTRYS, &entry) == -1) {
		Com_DPrintf("ioctl cdromreadtocentry failed\n");
		return;
	}
	if (toc_buffer.control == CDROM_DATA_TRACK) {
		Com_Printf("CDAudio: track %i is not audio\n", track);
		return;
	}
	if (playing) {
		if (playTrack == track)
			return;
		CDAudio_Stop();
	}
	ti.start_track = track;
	ti.end_track = track;
	ti.start_index = 1;
	ti.end_index = 99;

	if (ioctl(cdfile, CDIOCPLAYTRACKS, &ti) == -1) {
		Com_DPrintf("ioctl cdromplaytrkind failed\n");
		return;
	}
	if (ioctl(cdfile, CDIOCRESUME) == -1)
		Com_DPrintf("ioctl cdromresume failed\n");

	playLooping = looping;
	playTrack = track;
	playing = true;

	if (cd_volume->value == 0.0)
		CDAudio_Pause();
}

void
CDAudio_RandomPlay(void)
{
	int		track, i = 0, free_tracks = 0, remap_track;
	float		f;
	byte           *track_bools;
	struct ioc_read_toc_entry entry;
	struct cd_toc_entry toc_buffer;
	struct ioc_play_track ti;

	if (cdfile == -1 || !enabled)
		return;

	track_bools = (byte *) Q_malloc(maxTrack * sizeof(byte));

	if (track_bools == 0)
		return;

	/* create array of available audio tracknumbers */

	for (; i < maxTrack; i++) {
		bzero((char *)&toc_buffer, sizeof(toc_buffer));
		entry.data_len = sizeof(toc_buffer);
		entry.data = &toc_buffer;

		entry.starting_track = remap[i];
		entry.address_format = CD_LBA_FORMAT;
		if (ioctl(cdfile, CDIOREADTOCENTRYS, &entry) == -1) {
			track_bools[i] = 0;
		} else
			track_bools[i] = (entry.data->control != CDROM_DATA_TRACK);

		free_tracks += track_bools[i];
	}

	if (!free_tracks) {
		Com_DPrintf("CDAudio_RandomPlay: Unable to find and play a random audio track, insert an audio cd please");
		goto free_end;
	}
	/* choose random audio track */
	do {
		do {
			f = ((float)rand()) / ((float)RAND_MAX + 1.0);
			track = (int)(maxTrack * f);
		}
		while (!track_bools[track]);

		remap_track = remap[track];

		if (playing) {
			if (playTrack == remap_track) {
				goto free_end;
			}
			CDAudio_Stop();
		}
		ti.start_track = remap_track;
		ti.end_track = remap_track;
		ti.start_index = 0;
		ti.end_index = 0;

		if (ioctl(cdfile, CDROMPLAYTRKIND, &ti) == -1) {
			track_bools[track] = 0;
			free_tracks--;
		} else {
			playLooping = true;
			playTrack = remap_track;
			playing = true;
			break;
		}
	}
	while (free_tracks > 0);

free_end:
	Q_free((void *)track_bools);
}

void
CDAudio_Stop(void)
{
	if (cdfile == -1 || !enabled)
		return;

	if (!playing)
		return;

	if (ioctl(cdfile, CDIOCSTOP) == -1)
		Com_DPrintf("ioctl cdiocstop failed (%d)\n", errno);

	wasPlaying = false;
	playing = false;
}

void
CDAudio_Pause(void)
{
	if (cdfile == -1 || !enabled)
		return;

	if (!playing)
		return;

	if (ioctl(cdfile, CDIOCPAUSE) == -1)
		Com_DPrintf("ioctl cdiocpause failed\n");

	wasPlaying = playing;
	playing = false;
}


void
CDAudio_Resume(void)
{
	if (cdfile == -1 || !enabled)
		return;

	if (!cdValid)
		return;

	if (!wasPlaying)
		return;

	if (ioctl(cdfile, CDIOCRESUME) == -1)
		Com_DPrintf("ioctl cdiocresume failed\n");
	playing = true;
}

static void
CD_f(void)
{
	char           *command;
	int		ret;
	int		n;

	if (Cmd_Argc() < 2)
		return;

	command = Cmd_Argv(1);

	if (Q_strcasecmp(command, "on") == 0) {
		enabled = true;
		return;
	}
	if (Q_strcasecmp(command, "off") == 0) {
		if (playing)
			CDAudio_Stop();
		enabled = false;
		return;
	}
	if (Q_strcasecmp(command, "reset") == 0) {
		enabled = true;
		if (playing)
			CDAudio_Stop();
		for (n = 0; n < 100; n++)
			remap[n] = n;
		CDAudio_GetAudioDiskInfo();
		return;
	}
	if (Q_strcasecmp(command, "remap") == 0) {
		ret = Cmd_Argc() - 2;
		if (ret <= 0) {
			for (n = 1; n < 100; n++)
				if (remap[n] != n)
					Com_Printf("  %u -> %u\n", n, remap[n]);
			return;
		}
		for (n = 1; n <= ret; n++)
			remap[n] = atoi(Cmd_Argv(n + 1));
		return;
	}
	if (Q_strcasecmp(command, "close") == 0) {
		CDAudio_CloseDoor();
		return;
	}
	if (!cdValid) {
		CDAudio_GetAudioDiskInfo();
		if (!cdValid) {
			Com_Printf("No CD in player.\n");
			return;
		}
	}
	if (Q_strcasecmp(command, "play") == 0) {
		CDAudio_Play((byte) atoi(Cmd_Argv(2)), false);
		return;
	}
	if (Q_strcasecmp(command, "loop") == 0) {
		CDAudio_Play((byte) atoi(Cmd_Argv(2)), true);
		return;
	}
	if (Q_strcasecmp(command, "stop") == 0) {
		CDAudio_Stop();
		return;
	}
	if (Q_strcasecmp(command, "pause") == 0) {
		CDAudio_Pause();
		return;
	}
	if (Q_strcasecmp(command, "resume") == 0) {
		CDAudio_Resume();
		return;
	}
	if (Q_strcasecmp(command, "eject") == 0) {
		if (playing)
			CDAudio_Stop();
		CDAudio_Eject();
		cdValid = false;
		return;
	}
	if (!Q_strcasecmp(command, "random")) {
		CDAudio_RandomPlay();
		return;
	}
	if (Q_strcasecmp(command, "info") == 0) {
		Com_Printf("%u tracks\n", maxTrack);
		if (playing)
			Com_Printf("Currently %s track %u\n", playLooping ? "looping" : "playing", playTrack);
		else if (wasPlaying)
			Com_Printf("Paused %s track %u\n", playLooping ? "looping" : "playing", playTrack);
		Com_Printf("Volume is %f\n", cdvolume);
		return;
	}
}

void
CDAudio_Update(void)
{
	struct ioc_read_subchannel subchnl;
	struct cd_sub_channel_info data;
	static time_t	lastchk;

	if (cdfile == -1 || !enabled)
		return;

	if (cd_volume && cd_volume->value != cdvolume) {
		if (cdvolume) {
			Cvar_SetValue("cd_volume", 0.0);
			cdvolume = cd_volume->value;
			CDAudio_Pause();
		} else {
			Cvar_SetValue("cd_volume", 1.0);
			cdvolume = cd_volume->value;
			CDAudio_Resume();
		}
	}
	if (playing && lastchk < time(NULL)) {
		lastchk = time(NULL) + 2;	/* two seconds between chks */
		subchnl.address_format = CD_MSF_FORMAT;
		subchnl.data_format = CD_CURRENT_POSITION;
		subchnl.data_len = sizeof(data);
		subchnl.track = playTrack;
		subchnl.data = &data;
		if (ioctl(cdfile, CDIOCREADSUBCHANNEL, &subchnl) == -1) {
			Com_DPrintf("ioctl cdiocreadsubchannel failed\n");
			playing = false;
			return;
		}
		if (subchnl.data->header.audio_status != CD_AS_PLAY_IN_PROGRESS &&
		    subchnl.data->header.audio_status != CD_AS_PLAY_PAUSED) {
			playing = false;
			if (playLooping)
				CDAudio_Play(playTrack, true);
		}
	}
}

int
CDAudio_Init(void)
{
	int		i;
	cvar_t         *cv;
	extern uid_t	saved_euid;

	if (initialized)
		return 0;

	cv = Cvar_Get("nocdaudio", "0", CVAR_NOSET);
	if (cv->value)
		return -1;

	cd_nocd = Cvar_Get("cd_nocd", "0", CVAR_ARCHIVE);
	if (cd_nocd->value)
		return -1;

	cd_volume = Cvar_Get("cd_volume", "1", CVAR_ARCHIVE);

	cd_dev = Cvar_Get("cd_dev", "/dev/acd0", CVAR_ARCHIVE);

	seteuid(saved_euid);

	cdfile = open(cd_dev->string, O_RDONLY | O_NONBLOCK | O_EXCL);

	seteuid(getuid());

	if (cdfile == -1) {
		Com_Printf("CDAudio_Init: open of \"%s\" failed (%i)\n", cd_dev->string, errno);

		cdfile = -1;
		return -1;
	}
	for (i = 0; i < 100; i++)
		remap[i] = i;
	initialized = true;
	enabled = true;

	if (CDAudio_GetAudioDiskInfo()) {
		Com_Printf("CDAudio_Init: No CD in player.\n");
		cdValid = false;
	}
	Cmd_AddCommand("cd", CD_f);

	Com_Printf("CD Audio Initialized\n");

	return 0;
}

void
CDAudio_Activate(qboolean active)
{
	if (active)
		CDAudio_Resume();
	else
		CDAudio_Pause();
}

void
CDAudio_Shutdown(void)
{
	if (!initialized)
		return;
	CDAudio_Stop();
	close(cdfile);
	cdfile = -1;
	initialized = false;
}

#endif /* CD_AUDIO */
