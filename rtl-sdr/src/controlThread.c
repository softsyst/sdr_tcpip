/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
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
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

#ifdef NEED_PTHREADS_WORKARROUND
#define HAVE_STRUCT_TIMESPEC
#endif
#include <pthread.h>

#include "rtl-sdr.h"
#include "rtl_tcp.h"
//#include "convenience/convenience.h"

#include "tuner_r82xx.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")

typedef int socklen_t;

#else
#define closesocket close
#define SOCKADDR struct sockaddr
#define SOCKET int
#define SOCKET_ERROR -1
#endif

#define NUM_I2C_REGISTERS  32
#define TX_BUF_LEN (NUM_I2C_REGISTERS+3) //2 len, 1 command

const int REPORT_I2C_REGS = 0x48;   /* perodically report I2C registers */


ctrl_thread_data_t ctrl_thread_data;

void *ctrl_thread_fn(void *arg)
{

	unsigned char reg_values [NUM_I2C_REGISTERS];
	unsigned char txbuf [NUM_I2C_REGISTERS+3]; //1 command, 2 length
	int r = 1;
	struct timeval tv = { 1,0 };
	struct linger ling = { 1,0 };
	SOCKET listensocket;
	SOCKET controlSocket;
	struct sockaddr_in local, remote;
	socklen_t rlen;

	int error = 0;
	int ret = 0, len;
	fd_set connfds;
	fd_set writefds;
	int bytesleft, bytessent, index;

	ctrl_thread_data_t *data = (ctrl_thread_data_t *)arg;

	rtlsdr_dev_t *dev = data->dev;
	int port = data->port;
	int wait = data->wait;
	char *addr = data->addr;
	int* do_exit = data->pDoExit;
	u_long blockmode = 1;

	memset(reg_values, 0, NUM_I2C_REGISTERS);


	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = inet_addr(addr);

	listensocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	setsockopt(listensocket, SOL_SOCKET, SO_REUSEADDR, (char *)&r, sizeof(int));
	setsockopt(listensocket, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));
	int retval = bind(listensocket, (struct sockaddr *)&local, sizeof(local));
	if (retval == SOCKET_ERROR)
		error = WSAGetLastError();
#ifdef _WIN32
	ioctlsocket(listensocket, FIONBIO, &blockmode);
#else
	r = fcntl(listensocket, F_GETFL, 0);
	r = fcntl(listensocket, F_SETFL, r | O_NONBLOCK);
#endif

	while (1) {
		printf("listening on Control port %d...\n", port);
		retval = listen(listensocket, 1);
		if (retval == SOCKET_ERROR)
#ifdef _WIN32
			error = WSAGetLastError();
#else
			;
#endif
		while (1) {
			FD_ZERO(&connfds);
			FD_SET(listensocket, &connfds);
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			r = select(listensocket + 1, &connfds, NULL, NULL, &tv);
			if (*do_exit) {
				goto close;
			}
			else if (r) {
				rlen = sizeof(remote);
				controlSocket = accept(listensocket, (struct sockaddr *)&remote, &rlen);
				break;
			}
		}

		setsockopt(controlSocket, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling));

		printf("Control client accepted!\n");
		usleep(5000000);

		while (1) {
			int result = rtlsdr_get_tuner_i2c_register(dev, reg_values, NUM_I2C_REGISTERS);
			memset(txbuf, 0, TX_BUF_LEN);
			if (result)
				goto sleep;

			//Big Endian / Network Byte Order
			txbuf[0] = REPORT_I2C_REGS;
			txbuf[1] = 0;
			txbuf[2] = NUM_I2C_REGISTERS;
			memcpy(&txbuf[3], reg_values, NUM_I2C_REGISTERS);
			len = sizeof(txbuf);
			bytessent = 0;
			bytesleft = len;
			index = 0;

			while (bytesleft > 0) {
				FD_ZERO(&writefds);
				FD_SET(controlSocket, &writefds);
				tv.tv_sec = 1;
				tv.tv_usec = 0;
				r = select(controlSocket + 1, NULL, &writefds, NULL, &tv);
				if (r) {
					bytessent = send(controlSocket, &txbuf[index], bytesleft, 0);
					bytesleft -= bytessent;
					index += bytessent;
				}
				if (bytessent == SOCKET_ERROR || *do_exit) {
					goto close;
				}
			}
sleep:
			usleep(wait);
		}
close:
		closesocket(controlSocket);
		if (*do_exit)
		{
			closesocket(listensocket);
			printf("Control Thread terminates\n");
			break;
		}
	}
	return 0;
}

