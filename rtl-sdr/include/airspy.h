/*
Copyright (c) 2012, Jared Boone <jared@sharebrained.com>
Copyright (c) 2013, Michael Ossmann <mike@ossmann.com>
Copyright (c) 2013-2016, Benjamin Vernoux <bvernoux@airspy.com>
Copyright (C) 2013-2016, Youssef Touil <youssef@airspy.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

		Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
		Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the 
	documentation and/or other materials provided with the distribution.
		Neither the name of AirSpy nor the names of its contributors may be used to endorse or promote products derived from this software
	without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Function prototypes copied from qt-dab, Jan van Katwijk, Lazy Chair Computing
Modification by Clem Schmidt, https://qirx.softsyst.com, for QIRX
*/

#ifndef __AIRSPY_H__
#define __AIRSPY_H__

#include <stdint.h>
#include "airspy_commands.h"

#define AIRSPY_VERSION "1.0.9"
#define AIRSPY_VER_MAJOR 1
#define AIRSPY_VER_MINOR 0
#define AIRSPY_VER_REVISION 9

#ifdef _WIN32
	 #define ADD_EXPORTS
	 
	/* You should define ADD_EXPORTS *only* when building the DLL. */
	#ifdef ADD_EXPORTS
		#define ADDAPI //__declspec(dllexport)
	#else
		#define ADDAPI __declspec(dllimport)
	#endif

	/* Define calling convention in one place, for convenience. */
	#define ADDCALL __cdecl

#else /* _WIN32 not defined. */

	/* Define with no value on non-Windows OSes. */
	#define ADDAPI
	#define ADDCALL

#endif

#ifdef __cplusplus
extern "C"
{
#endif

enum airspy_error
{
	AIRSPY_SUCCESS = 0,
	AIRSPY_TRUE = 1,
	AIRSPY_ERROR_INVALID_PARAM = -2,
	AIRSPY_ERROR_NOT_FOUND = -5,
	AIRSPY_ERROR_BUSY = -6,
	AIRSPY_ERROR_NO_MEM = -11,
	AIRSPY_ERROR_LIBUSB = -1000,
	AIRSPY_ERROR_THREAD = -1001,
	AIRSPY_ERROR_STREAMING_THREAD_ERR = -1002,
	AIRSPY_ERROR_STREAMING_STOPPED = -1003,
	AIRSPY_ERROR_OTHER = -9999,
};

enum airspy_board_id
{
	AIRSPY_BOARD_ID_PROTO_AIRSPY  = 0,
	AIRSPY_BOARD_ID_INVALID = 0xFF,
};

enum airspy_sample_type
{
	AIRSPY_SAMPLE_FLOAT32_IQ = 0,   /* 2 * 32bit float per sample */
	AIRSPY_SAMPLE_FLOAT32_REAL = 1, /* 1 * 32bit float per sample */
	AIRSPY_SAMPLE_INT16_IQ = 2,     /* 2 * 16bit int per sample */
	AIRSPY_SAMPLE_INT16_REAL = 3,   /* 1 * 16bit int per sample */
	AIRSPY_SAMPLE_UINT16_REAL = 4,  /* 1 * 16bit unsigned int per sample */
	AIRSPY_SAMPLE_RAW = 5,          /* Raw packed samples from the device */
	AIRSPY_SAMPLE_END = 6           /* Number of supported sample types */
};

#define MAX_CONFIG_PAGE_SIZE (0x10000)

struct airspy_device;

typedef struct {
	struct airspy_device* device;
	void* ctx;
	void* samples;
	int sample_count;
	uint64_t dropped_samples;
	enum airspy_sample_type sample_type;
} airspy_transfer_t, airspy_transfer;

typedef struct {
	uint32_t part_id[2];
	uint32_t serial_no[4];
} airspy_read_partid_serialno_t;

typedef struct {
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t revision;
} airspy_lib_version_t;



typedef int(*airspy_sample_block_cb_fn)(airspy_transfer* transfer);

typedef void(*pfn_airspy_lib_version)(airspy_lib_version_t*);

typedef	int(*pfn_airspy_init) (void);
typedef int(*pfn_airspy_exit) (void);
typedef int(*pfn_airspy_open) (struct airspy_device**);
typedef int(*pfn_airspy_close) (struct airspy_device*);
typedef int(*pfn_airspy_get_samplerates) (struct airspy_device* device,
	uint32_t* buffer,
	const uint32_t len);
typedef int(*pfn_airspy_set_samplerate) (struct airspy_device* device,
	uint32_t samplerate);
typedef int(*pfn_airspy_start_rx) (struct airspy_device* device,
	airspy_sample_block_cb_fn callback,
	void* rx_ctx);
typedef int(*pfn_airspy_stop_rx) (struct airspy_device* device);

typedef int(*pfn_airspy_set_sample_type) (struct airspy_device *,
	 enum airspy_sample_type);
typedef int(*pfn_airspy_set_freq) (struct airspy_device* device,
	const uint32_t freq_hz);

typedef int(*pfn_airspy_set_lna_gain) (struct airspy_device* device,
	uint8_t value);

typedef int(*pfn_airspy_set_mixer_gain) (struct airspy_device* device,
	uint8_t value);

typedef int(*pfn_airspy_set_vga_gain) (struct airspy_device*
	device, uint8_t
	value);
typedef int(*pfn_airspy_set_lna_agc) (struct airspy_device* device,
	uint8_t value);
typedef int(*pfn_airspy_set_mixer_agc) (struct airspy_device* device,
	uint8_t value);

typedef int(*pfn_airspy_set_rf_bias) (struct airspy_device* dev,
	uint8_t value);

typedef const char* (*pfn_airspy_error_name) (enum airspy_error errcode);
typedef int(*pfn_airspy_board_id_read) (struct airspy_device *,
	uint8_t *);
typedef const char* (*pfn_airspy_board_id_name) (enum airspy_board_id board_id);
typedef int(*pfn_airspy_board_partid_serialno_read)(struct airspy_device* device, airspy_read_partid_serialno_t* read_partid_serialno);

typedef int(*pfn_airspy_set_linearity_gain) (struct airspy_device* device, uint8_t value);
typedef int(*pfn_airspy_set_sensitivity_gain)(struct airspy_device* device, uint8_t value);
typedef int(*pfn_airspy_set_packing) (struct airspy_device* device, uint8_t value);
typedef int(*pfn_airspy_r820t_write) (struct airspy_device* device, uint8_t register_number, uint8_t value);
typedef int(*pfn_airspy_r820t_read) (struct airspy_device* device, uint8_t register_number, uint8_t* value);


#ifdef __cplusplus
} // __cplusplus defined.
#endif

#endif//__AIRSPY_H__
