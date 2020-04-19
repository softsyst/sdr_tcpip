/*
 * Copyright (C) 2019 by Hayati Ayguen <h_ayguen@web.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "wavewrite.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#else
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#define _USE_MATH_DEFINES
#endif

#include <math.h>

#ifndef _WIN32

void executeInBackground( char * file, char * args, char * searchStr[], char * replaceStr[] )
{
	pid_t pid;
	char * argv[256] = { NULL };
	int k, argc = 0;
	argv[argc++] = file;
	if (args) {
		argv[argc] = strtok(args, " ");
		while (argc < 256 && argv[argc]) {
			argv[++argc] = strtok(NULL, " ");
			for (k=0; argv[argc] && searchStr && replaceStr && searchStr[k] && replaceStr[k]; k++) {
				if (!strcmp(argv[argc], searchStr[k])) {
					argv[argc] = replaceStr[k];
					break;
				}
			}
		}
	}

	pid = fork();
	switch (pid)
	{
	case -1:
		/* Fork() has failed */
		fprintf(stderr, "error: fork for '%s' failed!\n", file);
		break;
	case 0:
		execvp(file, argv);
		fprintf(stderr, "error: execv of '%s' from within fork failed!\n", file);
		exit(10);
		break;
	default:
		/* This is processed by the parent */
		break;
	}
}

#else

void executeInBackground( char * file, char * args, char * searchStr[], char * replaceStr[] )
{
	char * argv[256] = { NULL };
	int k, argc = 0;
	argv[argc++] = file;
 	if (args) {
		argv[argc] = strtok(args, " \t");
		while (argc < 256 && argv[argc]) {
			argv[++argc] = strtok(NULL, " \t");
			for (k=0; argv[argc] && searchStr && replaceStr && searchStr[k] && replaceStr[k]; k++) {
				if (!strcmp(argv[argc], searchStr[k])) {
					argv[argc] = replaceStr[k];
					break;
				}
			}
		}
	}

	spawnvp(P_NOWAIT, file, argv);
}

#endif


#pragma pack(push)
#pragma pack(1)

typedef struct {
	uint16_t	wYear;			/* 1601 through 30827 */
	uint16_t	wMonth;			/* 1..12 */
	uint16_t	wDayOfWeek;		/* 0 .. 6: 0 == Sunday, .., 6 == Saturday */
	uint16_t	wDay;			/* 1 .. 31 */
	uint16_t	wHour;			/* 0 .. 23 */
	uint16_t	wMinute;		/* 0 .. 59 */
	uint16_t	wSecond;		/* 0 .. 59 */
	uint16_t	wMilliseconds;	/* 0 .. 999 */
} Wind_SystemTime;


typedef struct
{
	/* RIFF header */
	char		riffID[4];	/* "RIFF" string */
	uint32_t	riffSize;	/* full filesize - 8 bytes (maybe with some byte missing...) */
	char		waveID[4];	/* "WAVE" string */

	/* FMT header */
	char		fmtID[4];	/* = "FMT " */
	uint32_t	fmtSize;
	int16_t		wFormatTag;
	int16_t		nChannels;
	int32_t		nSamplesPerSec;
	int32_t		nAvgBytesPerSec;
	int16_t		nBlockAlign;
	int16_t		nBitsPerSample;

	/* auxi header - used by SpectraVue / rfspace / HDSDR / .. */
	char		auxiID[4];	/* ="auxi" (chunk rfspace) */
	uint32_t	auxiSize;
	Wind_SystemTime StartTime;
	Wind_SystemTime StopTime;
	uint32_t	centerFreq;		/* receiver center frequency */
	uint32_t	ADsamplerate;	/* A/D sample frequency before downsampling */
	uint32_t	IFFrequency;	/* IF freq if an external down converter is used */
	uint32_t	Bandwidth;		/* displayable BW if you want to limit the display to less than Nyquist band */
	int32_t		IQOffset;		/* DC offset of the I and Q channels in 1/1000's of a count */
	int32_t		Unused2;
	int32_t		Unused3;
	int32_t		Unused4;
	int32_t		Unused5;

	/* DATA header */
	char		dataID[4];
	uint32_t	dataSize;
} waveFileHeader;

static waveFileHeader waveHdr;

#pragma pack(pop)


uint32_t	waveDataSize = 0;
static int	waveHdrStarted = 0;

void waveSetTime(Wind_SystemTime *p)
{
	struct timeval tv;
	struct tm t;

	gettimeofday(&tv, NULL);
	p->wMilliseconds = tv.tv_usec / 1000;

#ifdef _WIN32
	t = *gmtime(&tv.tv_sec);
#else
	gmtime_r(&tv.tv_sec, &t);
#endif

	p->wYear = t.tm_year + 1900;	/* 1601 through 30827 */
	p->wMonth = t.tm_mon + 1;		/* 1..12 */
	p->wDayOfWeek = t.tm_wday;		/* 0 .. 6: 0 == Sunday, .., 6 == Saturday */
	p->wDay = t.tm_mday;			/* 1 .. 31 */
	p->wHour = t.tm_hour;			/* 0 .. 23 */
	p->wMinute = t.tm_min;			/* 0 .. 59 */
	p->wSecond = t.tm_sec;			/* 0 .. 59 */
}

void wavePrepareHeader(unsigned samplerate, unsigned freq, int bitsPerSample, int numChannels)
{
	int	bytesPerSample = bitsPerSample / 8;
	int bytesPerFrame = bytesPerSample * numChannels;

	memcpy( waveHdr.riffID, "RIFF", 4 );
	waveHdr.riffSize = sizeof(waveFileHeader) - 8;		/* to fix */
	memcpy( waveHdr.waveID, "WAVE", 4 );

	memcpy( waveHdr.fmtID, "fmt ", 4 );
	waveHdr.fmtSize = 16;
	waveHdr.wFormatTag = 1;					/* PCM */
	waveHdr.nChannels = numChannels;		/* I and Q channels */
	waveHdr.nSamplesPerSec = samplerate;
	waveHdr.nAvgBytesPerSec = samplerate * bytesPerFrame;
	waveHdr.nBlockAlign = waveHdr.nChannels;
	waveHdr.nBitsPerSample = bitsPerSample;

	memcpy( waveHdr.auxiID, "auxi", 4 );
	waveHdr.auxiSize = 2 * sizeof(Wind_SystemTime) + 9 * sizeof(int32_t);  /* = 2 * 16 + 9 * 4 = 68 */
	waveSetTime( &waveHdr.StartTime );
	waveHdr.StopTime = waveHdr.StartTime;		/* to fix */
	waveHdr.centerFreq = freq;
	waveHdr.ADsamplerate = samplerate;
	waveHdr.IFFrequency = 0;
	waveHdr.Bandwidth = 0;
	waveHdr.IQOffset = 0;
	waveHdr.Unused2 = 0;
	waveHdr.Unused3 = 0;
	waveHdr.Unused4 = 0;
	waveHdr.Unused5 = 0;

	memcpy( waveHdr.dataID, "data", 4 );
	waveHdr.dataSize = 0;		/* to fix later */
	waveDataSize = 0;
}

void waveWriteHeader(unsigned samplerate, unsigned freq, int bitsPerSample, int numChannels, FILE * f)
{
	if (f != stdout) {
		assert( !waveHdrStarted );
		wavePrepareHeader(samplerate, freq, bitsPerSample, numChannels);
		fwrite(&waveHdr, sizeof(waveFileHeader), 1, f);
		waveHdrStarted = 1;
	}
}

void waveFinalizeHeader(FILE * f)
{
	if (f != stdout) {
		assert( waveHdrStarted );
		waveSetTime( &waveHdr.StopTime );
		waveHdr.dataSize = waveDataSize;
		waveHdr.riffSize += waveDataSize;

		fseek(f, 0, SEEK_SET);
		fwrite(&waveHdr, sizeof(waveFileHeader), 1, f);
		waveHdrStarted = 0;
	}
}
