/*
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012-2013 by Hoernchen <la@tfc-server.de>
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
 *
 * Airspy port :
 * Copyright (C) 2018 by Thierry Leconte http://www.github.com/TLeconte
 *
 * Modifications QIRX :
 * Copyright (C) 2019 by Clem Schmidt https://qirx.softsyst.com
 *
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdexcept>

#ifndef _WIN32
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>
#else
#include <winsock2.h>
#include "getopt/getopt.h"
#define usleep(x) Sleep(x/1000)
#endif
#include <pthread.h>
#include <libusb.h>
//#include "convenience/convenience.h"

#include <airspy.h>
#include <airspy_tcp.h>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")

typedef int socklen_t;

#else
#define closesocket close
#define SOCKADDR struct sockaddr
#define SOCKET int
#define SOCKET_ERROR -1
#endif

#ifdef __cplusplus
extern "C"
{
	bool	load_airspyFunctions(HINSTANCE Handle);

	extern pfn_airspy_lib_version airspy_lib_version;
	extern pfn_airspy_init		   airspy_init;
	extern pfn_airspy_exit		   airspy_exit;
	extern pfn_airspy_error_name	   airspy_error_name;
	extern pfn_airspy_open		   airspy_open;
	extern pfn_airspy_close	   airspy_close;
	extern pfn_airspy_get_samplerates airspy_get_samplerates;
	extern pfn_airspy_set_samplerate  airspy_set_samplerate;
	extern pfn_airspy_start_rx	   airspy_start_rx;
	extern pfn_airspy_stop_rx	   airspy_stop_rx;
	extern pfn_airspy_set_sample_type airspy_set_sample_type;
	extern pfn_airspy_set_freq	   airspy_set_freq;
	extern pfn_airspy_set_lna_gain	   airspy_set_lna_gain;
	extern pfn_airspy_set_mixer_gain  airspy_set_mixer_gain;
	extern pfn_airspy_set_vga_gain	   airspy_set_vga_gain;
	extern pfn_airspy_set_linearity_gain airspy_set_linearity_gain;
	extern pfn_airspy_set_sensitivity_gain airspy_set_sensitivity_gain;
	extern pfn_airspy_set_lna_agc	   airspy_set_lna_agc;
	extern pfn_airspy_set_mixer_agc   airspy_set_mixer_agc;
	extern pfn_airspy_set_rf_bias	   airspy_set_rf_bias;
	extern pfn_airspy_board_id_read   airspy_board_id_read;
	extern pfn_airspy_board_id_name   airspy_board_id_name;
	extern pfn_airspy_board_partid_serialno_read airspy_board_partid_serialno_read;
	extern pfn_airspy_set_packing airspy_set_packing;
	extern pfn_airspy_r820t_write airspy_r820t_write;
	extern pfn_airspy_r820t_read airspy_r820t_read;


	static SOCKET s;

	// -cs- Concurrent lock for the ...
#ifdef __MINGW32__
	static pthread_mutex_t cs_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
	static pthread_mutex_t cs_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif
	static pthread_t tcp_worker_thread;
	static pthread_t command_thread;
	static pthread_cond_t exit_cond;
	static pthread_mutex_t exit_cond_lock;

	static pthread_mutex_t ll_mutex;
	static pthread_cond_t cond;

	struct llist {
		char *data;
		size_t len;
		struct llist *next;
	};

	typedef struct { /* structure size must be multiple of 2 bytes */
		char magic[4];
		uint32_t tuner_type;
		uint32_t tuner_gain_count;
	} dongle_info_t;

	static struct airspy_device* dev = NULL;
	static uint32_t fscount, *supported_samplerates;
	static int verbose = 0;
	static int ppm_error = 0;
	static int frequencyHz = 100000000;
	static int BitWidth = 2; // 16-Bit default
	static int dshift = 1;

	static int enable_biastee = 0;
	static int global_numq = 0;

	static struct llist *ls_buffer = NULL;
	static struct llist *le_buffer = NULL;
	static int llbuf_num = 64;

	static volatile int do_exit = 0;



	void usage(void)
	{
		printf("airspy_tcp, a rtl-tcp compatible, I/Q server for airspy SDR\n\n"
			"Usage:\t[-a listen address]\n"
			"\t[-p listen port (default: 1234)]\n"
			"\t[-f frequency to tune to [Hz]]\n"
			"\t[-g gain (default: 0 for auto)]\n"
			"\t[-s samplerate in Hz . For DAB usage, 4096000 must be used]\n"
			"\t[-n max number of linked list buffer to keep ]\n"
			"\t[-T enable bias-T ]\n"
			"\t[-P ppm_error (default: 0) ]\n"
			//"\t[-D g digital shift (default : 1) ]\n"
			"\t[-v Verbose ]\n");
		exit(1);
	}

	static void sighandler(int signum)
	{
		do_exit = 1;
		pthread_cond_signal(&cond);
	}

	/**
	uint16_t rtlsdr_demod_read_reg(airspy_device *dev, uint8_t page, uint16_t addr, uint8_t len)
	{
		int r;
		unsigned char data[2];

		uint16_t index = page;
		uint16_t reg;
		addr = (addr << 8) | 0x20;

		r = libusb_control_transfer(dev->devh, CTRL_IN, 0, addr, index, data, len, CTRL_TIMEOUT);

		if (r < 0)
			fprintf(stderr, "%s failed with %d\n", __FUNCTION__, r);

		reg = (data[1] << 8) | data[0];

		return reg;
	}

	int rtlsdr_demod_write_reg(airspy_device *dev, uint8_t page, uint16_t addr, uint16_t val, uint8_t len)
	{
		int r;
		unsigned char data[2];
		uint16_t index = 0x10 | page;
		addr = (addr << 8) | 0x20;

		if (len == 1)
			data[0] = val & 0xff;
		else
			data[0] = val >> 8;

		data[1] = val & 0xff;

		r = libusb_control_transfer(dev->devh, CTRL_OUT, 0, addr, index, data, len, CTRL_TIMEOUT);

		if (r < 0)
			fprintf(stderr, "%s failed with %d\n", __FUNCTION__, r);

		rtlsdr_demod_read_reg(dev, 0x0a, 0x01, 1);

		return (r == len) ? 0 : -1;
	}

	void rtlsdr_set_i2c_repeater(airspy_device *dev, int on)
	{
		rtlsdr_demod_write_reg(dev, 1, 0x01, on ? 0x18 : 0x10, 1);
	}

*/
#define CTRL_IN			(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define CTRL_TIMEOUT	300

	uint16_t airspy_demod_read_reg(void *dev, uint8_t page, uint16_t addr, uint8_t len)
	{
		int r;
		unsigned char data[2];

		uint16_t index = page;
		uint16_t reg;
		addr = (addr << 8) | 0x20;

		_int64 p = *((_int64*)dev + 1);
		libusb_device_handle* devh = (libusb_device_handle*)(p);
		r = libusb_control_transfer(devh, CTRL_IN, 0, addr, index, data, len, CTRL_TIMEOUT);

		if (r < 0)
			fprintf(stderr, "%s failed with %d\n", __FUNCTION__, r);

		reg = (data[1] << 8) | data[0];

		return reg;
	}

	int airspy_demod_write_reg(void *dev, uint8_t page, uint16_t addr, uint16_t val, uint8_t len)
	{
		int r;
		unsigned char data[2];
		uint16_t index = 0x10 | page;
		addr = (addr << 8) | 0x20;

		if (len == 1)
			data[0] = val & 0xff;
		else
			data[0] = val >> 8;

		data[1] = val & 0xff;

		_int64 p = *((_int64*)dev +1);

		libusb_device_handle* devh = (libusb_device_handle*)(p);
		r = libusb_control_transfer(devh, CTRL_OUT, 0, addr, index, data, len, CTRL_TIMEOUT );

		if (r < 0)
			fprintf(stderr, "%s failed with %d\n", __FUNCTION__, r);

		airspy_demod_read_reg(dev, 0x0a, 0x01, 1);

		return (r == len) ? 0 : -1;
	}

	void airspy_set_i2c_repeater(airspy_device *dev, int on)
	{
		airspy_demod_write_reg(dev, 1, 0x01, on ? 0x18 : 0x10, 1);
	}


	static int started = 0;

	int airspy_get_tuner_register(airspy_device *dev, uint8_t* data, int len)
	{
		int r = 0;

		if (!dev )
			return -1;

		for (int i=0; i < len; i++)
		{
			if (started && (i >=20 ))// && i <=27 ))
			{
				//printf("Register %d skipped\n", i);
				//usleep(1000000);
				continue;
			}
			//airspy_set_i2c_repeater(dev, 1);
			//r = airspy_r820t_read(dev, i, &data[i]);

			_int64 p = *((_int64*)dev + 1);
			libusb_device_handle* devh = (libusb_device_handle*)(p);
			r = libusb_control_transfer(devh, 0xc0, 5, 0, (unsigned short)i, data+ (uint8_t)i,1,0);

			//usleep(1000000);
			if (r != AIRSPY_SUCCESS+1)
			{
				printf("Airspy read r820t failed at register %d with error%d\n", i, r);
				return r;
			}
			//printf("Register %d \n", i);
		}
		started = 1;
		return 0;
	}
	//-cs end


	static int rx_callback(airspy_transfer_t* transfer)
	{
		int len;

		len = BitWidth*2 * transfer->sample_count; //bw== 1: 8, bw==2: 16 Bit transfer

		if (!do_exit) {
			int i;
			char *data;
			struct llist *rpt;

			if (BitWidth == 2) // 16-Bit default
			{
				len = BitWidth*2 * transfer->sample_count; //bw== 1: 8, bw==2: 16 Bit transfer
				uint8_t *buf;
				buf = (uint8_t*)(transfer->samples);
				rpt = (struct llist*)malloc(sizeof(struct llist));
				rpt->data = (char*)malloc(len);
				rpt->len = len;
				rpt->next = NULL;
				memcpy(rpt->data, buf, len);
			}
			else if (BitWidth == 1) // 8-Bit 
			{
				len = BitWidth*2 * transfer->sample_count; //bw== 1: 8, bw==2: 16 Bit transfer
				short* pIn = (short*)(transfer->samples);
				rpt = (struct llist*)malloc(sizeof(struct llist));
				uint8_t *buf = (uint8_t*)malloc(len);
				rpt->data = (char*)buf;
				rpt->len = len;
				rpt->next = NULL;

				for (i = 0; i < len; i++)
				{
					//restore the unsigned 12-Bit signal
					int tmp = (pIn[i] >> 4) + 2048 ;

					// cut the four low order bits
					tmp >>= 4;

					buf[i] = tmp;// (uint8_t)((pIn[i] >> 6) + 127);	//I-data, Q-data alternating
				}
			}
			else if (BitWidth == 0) // 4-Bit 
			{
				len = 2 * transfer->sample_count;			//bw == 0: 4,  bw== 1: 8, bw==2: 16 Bit transfer
				short* pIn = (short*)(transfer->samples);
				rpt = (struct llist*)malloc(sizeof(struct llist));
				uint8_t *buf = (uint8_t*)malloc(len);
				rpt->data = (char*)buf;
				rpt->len = len;
				rpt->next = NULL;

				for (i = 0; i < len; i++)
				{
					//I-Byte
					uint8_t b = (uint8_t)(pIn[2*i] / 64 + 127);	//I-data
					// Low order nibble
					byte b2 = b >> 4;
					b2 &= 0x0f;

					//Q-Byte
					b = (uint8_t)(pIn[2*i +1] / 64 + 127);	//Q-data 
					// High order nibble
					byte b3 = b & 0xf0;

					byte b4 = b2 | b3;
					buf[i] = b4;
				}
			}
			pthread_mutex_lock(&ll_mutex);

			if (ls_buffer == NULL) {
				ls_buffer = le_buffer = rpt;
			}
			else {
				le_buffer->next = rpt;
				le_buffer = rpt;
			}
			global_numq++;

			if (global_numq > llbuf_num) {
				struct llist *curelem;
				curelem = ls_buffer;
				ls_buffer = ls_buffer->next;
				if (ls_buffer == NULL) le_buffer = NULL;
				global_numq--;
				free(curelem->data);
				free(curelem);
			}

			pthread_cond_signal(&cond);
			pthread_mutex_unlock(&ll_mutex);
		}
		return 0;
	}

	static void *tcp_worker(void *arg)
	{
		struct llist *curelem;
		int bytesleft, bytessent, index;
		struct timeval tv = { 1,0 };
		struct timespec ts;
		struct timeval tp;
		fd_set writefds;
		int r = 0;

		while (1) {

			pthread_mutex_lock(&ll_mutex);
			while (ls_buffer == NULL && do_exit == 0)
				pthread_cond_wait(&cond, &ll_mutex);

			if (do_exit) {
				pthread_mutex_unlock(&ll_mutex);
				pthread_exit(0);
			}

			curelem = ls_buffer;
			ls_buffer = ls_buffer->next;
			global_numq--;
			pthread_mutex_unlock(&ll_mutex);

			bytesleft = curelem->len;
			index = 0;
			while (bytesleft > 0) {
				//bytessent = send(s, &curelem->data[index], bytesleft, 0);
				//bytesleft -= bytessent;
				//index += bytessent;
				FD_ZERO(&writefds);
				FD_SET(s, &writefds);
				tv.tv_sec = 1;
				tv.tv_usec = 0;
				r = select(s + 1, NULL, &writefds, NULL, &tv);
				if (r) {
					bytessent = send(s, &curelem->data[index], bytesleft, 0);
					bytesleft -= bytessent;
					index += bytessent;
				}
				if (bytessent == SOCKET_ERROR || do_exit)
				{
					if (bytessent == SOCKET_ERROR)
					{
						printf("worker socket bye due to SOCKET_ERROR : %d\n", WSAGetLastError());
					}
					else if (do_exit)
					{
						printf("worker socket bye due to exit request\n");
					}
					sighandler(0);
					pthread_exit(NULL);
				}
				//if (bytessent == SOCKET_ERROR || do_exit) {
				//	printf("worker socket bye\n");
					//sighandler(0);
					//pthread_exit(NULL);
				//}
			}
			free(curelem->data);
			free(curelem);
		}
	}

#ifdef _WIN32
#define __attribute__(x)
#pragma pack(push, 1)
#endif
	struct command {
		unsigned char cmd;
		unsigned int param;
	}__attribute__((packed));


	static int set_agc(uint8_t value)
	{
		int r;

		r = airspy_set_lna_agc(dev, value);
		if (r != AIRSPY_SUCCESS)
		{
			printf("Set LNA AGC %d failed with %d\n", value, r);
			return r;
		}
		else
			printf("Set LNA AGC %d succeeded\n", value);


		r = airspy_set_mixer_agc(dev, value);
		if (r != AIRSPY_SUCCESS)
		{
			printf("Set Mixer AGC %d failed with %d\n", value, r);
			return r;
		}
		else
			printf("Set Mixer AGC %d succeeded\n", value);
		return r;
	}

	 
	// The undocumented 4096000 must be set directly
	static int set_samplerate(uint32_t fs)
	{
		int r, i;

		//test
		if (fs == 2048000)
			fs = 4096000;
			//fs = 3000000;

		// undocumented case
		if (fs == 4096000)
		{
			r = airspy_set_samplerate(dev, fs);
			if (r == AIRSPY_SUCCESS)
				printf("Set sample rate %d succeeded\n", fs);
			else
				printf("Set sample rate %d failed with error %d\n", fs, r);
			return r;
		}

		for (i = 0; i < fscount; i++)
		{
			if (fs == 2048000u && supported_samplerates[i] == 3000000u || supported_samplerates[i] == fs) 
				break;
			if (i >= fscount) {
				printf("sample rate %d not supported\n", fs);
				return AIRSPY_ERROR_INVALID_PARAM;
			}
		}

		r = airspy_set_samplerate(dev, i);
		if (r == AIRSPY_SUCCESS)
			printf("Set sample rate %d succeeded\n", supported_samplerates[i]);
		else
			printf("Set sample rate %d failed with error %d\n", fs, r);
		return r;
	}

	static int set_freq(uint32_t f)
	{
		frequencyHz = f;
		int r;
		uint32_t ff = (uint32_t)((float)f*(1.0 + (float)ppm_error / 1e6));
		r = airspy_set_freq(dev, ff);
		if (r != AIRSPY_SUCCESS)
			printf("Set frequency %d failed with %d", ff, r);
		return r;
	}

	static void *command_worker(void *arg)
	{
		int left, received = 0;
		fd_set readfds;
		struct command cmd = { 0, 0 };
		struct timeval tv = { 1, 0 };
		int r = 0;
		unsigned int tmp;

		while (1) {
			left = sizeof(cmd);
			while (left > 0) {
				FD_ZERO(&readfds);
				FD_SET(s, &readfds);
				tv.tv_sec = 1;
				tv.tv_usec = 0;
				r = select(s + 1, &readfds, NULL, NULL, &tv);
				if (r) {
					received = recv(s, (char*)&cmd + (sizeof(cmd) - left), left, 0);
					left -= received;
				}
				if (received == SOCKET_ERROR || do_exit) {
					printf("comm recv bye\n");
					sighandler(0);
					pthread_exit(NULL);
				}
			}
			//printf("Command %d arrived with parameter %d\n", cmd.cmd, ntohl(cmd.param));

			switch (cmd.cmd) {
			case SET_FREQUENCY://0x01
				if (verbose) printf("set freq %d\n", ntohl(cmd.param));
				set_freq(ntohl(cmd.param));
				break;
			case SET_SAMPLE_RATE://0x02
				if (verbose) printf("set sample rate : %d\n", ntohl(cmd.param));
				set_samplerate(ntohl(cmd.param));
				break;
			case SET_GAIN_MODE://0x03
				if (verbose) printf("set gain mode %d : not implemented \n", ntohl(cmd.param));
			case SET_GAIN://0x04
				//if (verbose) printf("set gain : %d\n", ntohl(cmd.param));
				//airspy_set_linearity_gain(dev, (ntohl(cmd.param) + 250) / 37);
				break;
			case SET_FREQUENCY_CORRECTION://0x05
				if (verbose) printf("set freq correction %d\n", ntohl(cmd.param));
				ppm_error = ntohl(cmd.param);
				set_freq(frequencyHz);
				break;
			case SET_IF_STAGE:
				if (verbose) printf("set if stage gain %d : not implemented\n", ntohl(cmd.param));
				break;
			case SET_TEST_MODE:
				if (verbose) printf("set test mode %d: not impmemented\n", ntohl(cmd.param));
				break;
			case SET_AGC_MODE://0x08
				//set_agc(ntohl(cmd.param));
				break;
			case SET_DIRECT_SAMPLING://0x09
				if (verbose) printf("set direct sampling %d: not implemented\n", ntohl(cmd.param));
				break;
			case SET_OFFSET_TUNING://0x0a
				if (verbose) printf("set offset tuning %d : not impemented\n", ntohl(cmd.param));
				break;
			case SET_RTL_CRYSTAL:
				if (verbose) printf("set rtl xtal %d : not implemented\n", ntohl(cmd.param));
				break;
			case SET_TUNER_CRYSTAL:
				if (verbose) printf("set tuner xtal %d : not implemented\n", ntohl(cmd.param));
				break;
			case SET_TUNER_GAIN_BY_INDEX:
				if (verbose) printf("set tuner gain by index %d \n", ntohl(cmd.param));
				//airspy_set_linearity_gain(dev, ntohl(cmd.param));
				airspy_set_vga_gain(dev, ntohl(cmd.param));
				break;
			case SET_BIAS_TEE:
				if (verbose) printf("set bias tee %d\n", ntohl(cmd.param));
				airspy_set_rf_bias(dev, (int)ntohl(cmd.param));
				break;
			case SET_I2C_TUNER_REGISTER://0x43
				tmp = ntohl(cmd.param);
				printf("set i2c register x%03X to x%03X with mask x%02X\n", (tmp >> 20) & 0xfff, tmp & 0xfff, (tmp >> 12) & 0xff);
				airspy_r820t_write(dev, (tmp >> 20) & 0xfff, tmp & 0xfff);
				//airspy_r820t_write(dev, (tmp >> 20) & 0xfff, (tmp >> 12) & 0xff, tmp & 0xfff);
				break;
			case SET_SIDEBAND://0x46
				//tmp = ntohl(cmd.param) & 1;
				//if (tmp)
				//	printf("set to upper sideband\n");
				//else
				//	printf("set to lower sideband\n");
				//rtlsdr_set_tuner_sideband(dev, tmp);
				break;
			default:
				break;
			}
			cmd.cmd = 0xff;
		}
	}



	int main(int argc, char *const * argv)
	{
		int r, opt, i;
		airspy_error rr = AIRSPY_SUCCESS;
		char* addr = "127.0.0.1";
		int port = 1234;
		uint32_t frequency = 100000000, samp_rate = 0;
		struct sockaddr_in local, remote;
		int gain = 0;
		struct llist *curelem, *prev;
		pthread_attr_t attr;
		void *status;
		struct timeval tv = { 1,0 };
		struct linger ling = { 1,0 };
		SOCKET listensocket;
		socklen_t rlen;
		fd_set readfds;
		u_long blockmode = 1;
		dongle_info_t dongle_info;
		pthread_t thread_ctrl; //-cs- for periodically reading the register values
		int do_exit_thrd_ctrl = 0;
		verbose = 1;

#ifdef _WIN32
		WSADATA wsd;
		i = WSAStartup(MAKEWORD(2, 2), &wsd);
		extern int getopt(int argc, char *const * argv, const char *__shortopts);
#else
		struct sigaction sigact, sigign;
#endif
		printf("airspy_tcp, an I/Q data server for Airspy receivers\n"
			"Version 0.13 for QIRX, 25.09.2020\n\n");

		//for (int k = 0; k < argc; k++)
		//{
		//	printf(argv[k]); 
		//	printf("\n"); 
		//}
		int lastError = 0;
		HINSTANCE Handle = LoadLibrary("libairspy.dll");
		if (Handle == 0)
		{
			printf("Error %d loading libairspy.dll\n", GetLastError());
			return -1;
		}

		if (!load_airspyFunctions(Handle))
		{
			printf("Cannot load functions from libairspy.dll library.\n");
			return -1;
		}

		airspy_lib_version_t libversion;
		airspy_lib_version(&libversion);

		printf("Airspy library version %d.%d.%d\n\n", libversion.major_version, libversion.minor_version, libversion.revision);

		while ((opt = getopt(argc, argv, "a:p:f:g:s:b:n:d:P:TD:W:v")) != -1) {
			switch (opt) {
			case 'f':
				frequency = (uint32_t)atoi(optarg);
				break;
			case 'g':
				gain = (int)(atof(optarg) * 10); /* tenths of a dB */
				break;
			case 's':
				samp_rate = (uint32_t)atoi(optarg);
				break;
			case 'a':
				addr = optarg;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'n':
				llbuf_num = atoi(optarg);
				break;
			case 'T':
				enable_biastee = 1;
				break;
			case 'P':
				ppm_error = atoi(optarg);
				break;
			case 'D':
				dshift = atoi(optarg);
				break;
			case 'v':
				verbose = 1;
				break;
			case 'b':
				break;
			case 'd':
				printf("Parameter d not supported.\n");
				break;

			case 'W':
				printf("Bit width %d selected.\n", atoi(optarg));
				BitWidth = atoi(optarg);
				break;

			default:
				usage();
				break;
			}
		}

		if (argc < optind)
			usage();

		try
		{
			r = airspy_open(&dev);
			if (r != AIRSPY_SUCCESS) {
				fprintf(stderr, "airspy_open() failed: %s (%d)\n", airspy_error_name(rr), r);
				airspy_exit();
				return -1;
			}

		}
		catch (const std::exception& e)
		{

		}

		r = airspy_set_sample_type(dev, AIRSPY_SAMPLE_INT16_IQ);
		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_set_sample_type() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

		airspy_set_packing(dev, 1);

		r = airspy_get_samplerates(dev, &fscount, 0);
		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_get_sample_rate() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

		// one more due to the undocumented 4096000
		supported_samplerates = (uint32_t *)malloc((fscount) * sizeof(uint32_t));
		r = airspy_get_samplerates(dev, supported_samplerates, fscount);
		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_get_sample_rate() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

		samp_rate = 4096000;

		if (samp_rate == 4096000) {
			rr = (airspy_error)airspy_set_samplerate(dev, samp_rate);
			if (rr != AIRSPY_SUCCESS) { 

				fprintf(stderr, "set_samplerate() failed: %s (%d)\n", airspy_error_name(rr), rr);
				airspy_close(dev);
				airspy_exit();
				return -1;
			}
		}
		else if (samp_rate) {
			rr = (airspy_error)set_samplerate(samp_rate);
			//rr = (airspy_error)airspy_set_samplerate(dev, 4096000);
			if (rr != AIRSPY_SUCCESS) { 

				fprintf(stderr, "set_samplerate() failed: %s (%d)\n", airspy_error_name(rr), rr);
				airspy_close(dev);
				airspy_exit();
				return -1;
			}
		}
		else {
			r = airspy_set_samplerate(dev, fscount - 1);
			if (r != AIRSPY_SUCCESS) {
				fprintf(stderr, "airspy_set_samplerate() failed: %s (%d)\n", airspy_error_name(rr), r);
				airspy_close(dev);
				airspy_exit();
				return -1;
			}
		}

		/* Set the frequency */
		r = set_freq(frequency);
		frequencyHz = frequency;

		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_set_freq() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

		//if (0 == gain) {
		//	/* Enable automatic gain */
		//r = set_agc(1);
		//	if (r != AIRSPY_SUCCESS) {
		//		fprintf(stderr, "airspy_set agc failed: %s (%d)\n", airspy_error_name(rr), r);
		//	}
		//}
		//else {
		//	r = airspy_set_linearity_gain(dev, (gain + 250) / 37);
		//	if (r != AIRSPY_SUCCESS) {
		//		fprintf(stderr, "set gains failed: %s (%d)\n", airspy_error_name(rr), r);
		//		airspy_close(dev);
		//		airspy_exit();
		//		return -1;
		//	}
		//	if (verbose) fprintf(stderr, "Tuner gain set to %f dB.\n", gain / 10.0);
		//}

		r = airspy_set_rf_bias(dev, enable_biastee);
		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_set_rf_bias() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

		set_agc(1);
		if (r != AIRSPY_SUCCESS) {
			fprintf(stderr, "set_agc() failed: %s (%d)\n", airspy_error_name(rr), r);
			airspy_close(dev);
			airspy_exit();
			return -1;
		}

#ifndef _WIN32
		sigact.sa_handler = sighandler;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = 0;
		sigign.sa_handler = SIG_IGN;
		sigaction(SIGINT, &sigact, NULL);
		sigaction(SIGTERM, &sigact, NULL);
		sigaction(SIGQUIT, &sigact, NULL);
		sigaction(SIGPIPE, &sigign, NULL);
#else
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)sighandler, TRUE);
#endif

		pthread_mutex_init(&exit_cond_lock, NULL);
		pthread_mutex_init(&ll_mutex, NULL);
		pthread_mutex_init(&exit_cond_lock, NULL);
		pthread_cond_init(&cond, NULL);
		pthread_cond_init(&exit_cond, NULL);

		// currently not used.
		ctrl_thread_data_t ctrldata = { dev, port + 1, 500000, addr, &do_exit_thrd_ctrl };
		pthread_create(&thread_ctrl, NULL, &ctrl_thread_fn, (void *)(&ctrldata));

		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_port = htons(port);
		local.sin_addr.s_addr = inet_addr(addr);

		listensocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		r = 1;
		setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, (char *)&r, sizeof(int));
		setsockopt(listensocket, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));
		bind(listensocket, (struct sockaddr *)&local, sizeof(local));

#ifdef _WIN32
		ioctlsocket(listensocket, FIONBIO, &blockmode);
#else
		r = fcntl(listensocket, F_GETFL, 0);
		r = fcntl(listensocket, F_SETFL, r | O_NONBLOCK);
#endif

		while (1) {
			printf("listening...\n");
			printf("Use the device argument 'rtl_tcp=%s:%d' in OsmoSDR "
				"(gr-osmosdr) source\n"
				"to receive samples in GRC and control "
				"parameters (frequency, gain, ...).\n",
				addr, port);
			listen(listensocket, 1);

			while (1) {
				FD_ZERO(&readfds);
				FD_SET(listensocket, &readfds);
				tv.tv_sec = 1;
				tv.tv_usec = 0;
				r = select(listensocket + 1, &readfds, NULL, NULL, &tv);
				if (do_exit) {
					goto out;
				}
				else if (r) {
					rlen = sizeof(remote);
					s = accept(listensocket, (struct sockaddr *)&remote, &rlen);
					break;
				}
			}

			setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));
			//r=5;setsockopt(s, SOL_SOCKET, SO_PRIORITY, (char *)&r, sizeof(int));

			printf("client accepted!\n");

			memset(&dongle_info, 0, sizeof(dongle_info));
			//memcpy(&dongle_info.magic, "RTL0", 4);
			memcpy(&dongle_info.magic, "ASPY", 4);

			dongle_info.tuner_type = htonl(TUNER_AIRSPY);
			dongle_info.tuner_type |= BitWidth << 16;			// Byte index 7
			dongle_info.tuner_gain_count = htonl(AIRSPY_VGA_GAIN_STEPS);

			r = send(s, (const char *)&dongle_info, sizeof(dongle_info), 0);
			if (sizeof(dongle_info) != r) {
				printf("failed to send dongle information\n");
				break;
			}

			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
			r = pthread_create(&tcp_worker_thread, &attr, tcp_worker, NULL);
			r = pthread_create(&command_thread, &attr, command_worker, NULL);
			pthread_attr_destroy(&attr);

			fprintf(stderr, "start rx\n");
			r = airspy_start_rx(dev, rx_callback, NULL);
			if (r != AIRSPY_SUCCESS) {
				fprintf(stderr, "airspy_start_rx() failed: %s (%d)\n", airspy_error_name(rr), r);
				break;
			}

			pthread_join(tcp_worker_thread, &status);
			pthread_join(command_thread, &status);

			closesocket(s);

			fprintf(stderr, "stop rx\n");

			r = airspy_stop_rx(dev);
			if (r != AIRSPY_SUCCESS) {
				fprintf(stderr, "airspy_stop_rx() failed: %s (%d)\n", airspy_error_name(rr), r);
				break;
			}

			curelem = ls_buffer;
			while (curelem != 0) {
				prev = curelem;
				curelem = curelem->next;
				free(prev->data);
				free(prev);
			}
			ls_buffer = le_buffer = NULL;
			global_numq = 0;

			do_exit = 0;
		}

	out:
		airspy_close(dev);
		airspy_exit();
		closesocket(listensocket);
		closesocket(s);
#ifdef _WIN32
	WSACleanup();
#endif
		printf("bye!\n");
		return r >= 0 ? r : -r;
	}
}
#endif
