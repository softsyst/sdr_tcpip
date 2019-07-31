/*
 * Rafael Micro R820T/R828D driver
 *
 * Copyright (C) 2013 Mauro Carvalho Chehab <mchehab@redhat.com>
 * Copyright (C) 2013 Steve Markgraf <steve@steve-m.de>
 *
 * This driver is a heavily modified version of the driver found in the
 * Linux kernel:
 * http://git.linuxtv.org/linux-2.6.git/history/HEAD:/drivers/media/tuners/r820t.c
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "rtlsdr_i2c.h"
#include "tuner_r82xx.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MHZ(x)		((x)*1000*1000)
#define KHZ(x)		((x)*1000)

/*
Reg		Bitmap	Symbol			Description
------------------------------------------------------------------------------------
R0		[7:0]	CHIP_ID			reference check point for read mode: 0x96
0x00
------------------------------------------------------------------------------------
R1		?
0x01
------------------------------------------------------------------------------------
R2		[7]						0
0x02	[6:0]	VCO_INDICATOR	Bit 6 = 1: PLL has locked
------------------------------------------------------------------------------------
R3		[7:4]	RF_INDICATOR	LNA gain
0x03							0: Lowest, 15: Highest
		[3:0]					Mixer gain
								0: Lowest, 15: Highest
------------------------------------------------------------------------------------
R4		[5:4]					vco_fine_tune
0x04	[3:0]					fil_cal_code
------------------------------------------------------------------------------------
R5		[7] 	PWD_LT			Loop through ON/OFF
0x05							0: on, 1: off
		[6]						0
		[5] 	PWD_LNA1		LNA 1 power control
								0:on, 1:off
		[4] 	LNA_GAIN_MODE	LNA gain mode switch
								0: auto, 1: manual
		[3:0] 	LNA_GAIN		LNA manual gain control
								15: max gain, 0: min gain
------------------------------------------------------------------------------------
R6		[7] 	PWD_PDET1		Power detector 1 on/off
0x06							0: on, 1: off
		[6] 	PWD_PDET3		Power detector 3 on/off
								0: off, 1: on
		[5] 	FILT_3DB		Filter gain 3db
								0:0db, 1:+3db
		[4:3]					10
		[2:0]	PW_LNA			LNA power control
								000: max, 111: min
------------------------------------------------------------------------------------
R7		[7]						Mixer Sideband
0x07							0: lower, 1: upper
		[6] 	PWD_MIX			Mixer power
								0:off, 1:on
		[5] 	PW0_MIX			Mixer current control
								0:max current, 1:normal current
		[4] 	MIXGAIN_MODE	Mixer gain mode
								0:manual mode, 1:auto mode
		[3:0] 	MIX_GAIN		Mixer manual gain control
								0000->min, 1111->max
------------------------------------------------------------------------------------
R8		[7] 	PWD_AMP			Mixer buffer power on/off
0x08							0: off, 1:on
		[6] 	PW0_AMP			Mixer buffer current setting
								0: high current, 1: low current
		[5:0] 	IMR_G			Image Gain Adjustment
								0: min, 63: max
------------------------------------------------------------------------------------
R9		[7] 	PWD_IFFILT		IF Filter power on/off
0x09							0: filter on, 1: off
		[6] 	PW1_IFFILT		IF Filter current
								0: high current, 1: low current
		[5:0] 	IMR_P			Image Phase Adjustment
								0: min, 63: max
------------------------------------------------------------------------------------
R10		[7] 	PWD_FILT		Filter power on/off
0x0A							0: channel filter off, 1: on
		[6:5] 	PW_FILT			Filter power control
								00: highest power, 11: lowest power
		[4]						1
		[3:0] 	FILT_CODE		Filter bandwidth manual fine tune
								0000 Widest, 1111 narrowest
------------------------------------------------------------------------------------
R11		[7:5] 	FILT_BW			Filter bandwidth manual course tunnel
0x0B							000: widest
								010 or 001: middle
								111: narrowest
		[4]						0
		[3:0] 	HPF				High pass filter corner control
								0000: highest
								1111: lowest
------------------------------------------------------------------------------------
R12		[7]						1
0x0C	[6] 	PWD_VGA			VGA power control
								0: vga power off, 1: vga power on
		[5]						1
		[4] 	VGA_MODE		VGA GAIN manual / pin selector
								1: IF vga gain controlled by vagc pin
								0: IF vga gain controlled by vga_code[3:0]
		[3:0] 	VGA_CODE		IF vga manual gain control
								0000: -12.0 dB
								1111: +40.5 dB; -3.5dB/step
------------------------------------------------------------------------------------
R13		[7:4]	LNA_VTHH		LNA agc power detector voltage threshold high setting
0x0D							1111: 1.94 V
								0000: 0.34 V, ~0.1 V/step
		[3:0] 	LNA_VTHL		LNA agc power detector voltage threshold low setting
								1111: 1.94 V
								0000: 0.34 V, ~0.1 V/step
------------------------------------------------------------------------------------
R14 	[7:4] 	MIX_VTH_H		MIXER agc power detector voltage threshold high setting
0x0E							1111: 1.94 V
								0000: 0.34 V, ~0.1 V/step
		[3:0] 	MIX_VTH_L		MIXER agc power detector voltage threshold low setting
								1111: 1.94 V
								0000: 0.34 V, ~0.1 V/step
------------------------------------------------------------------------------------
R15		[7]						filter extension widest
								0: off, 1: on
0x0F	[4] 	CLK_OUT_ENB		Clock out pin control
								0: clk output on, 1: off
		[3]						1
		[2]						set cali clk
								0: off, 1: on
		[1] 	CLK_AGC_ENB		AGC clk control
								0: internal agc clock on, 1: off
		[0]		GPIO			0
------------------------------------------------------------------------------------
R16		[7:5] 	SEL_DIV			PLL to Mixer divider number control
0x10							000: mixer in = vco out /2
								001: mixer in = vco out / 4
								010: mixer in = vco out / 8
								011: mixer in = vco out
		[4] 	REFDIV			PLL Reference frequency Divider
								0 -> fref=xtal_freq
								1 -> fref=xta_freql / 2 (for Xtal >24MHz)
		[3:2]					01
		[1:0] 	CAPX			Internal xtal cap setting
								00->no cap
								01->10pF
								10->20pF
								11->30pF
------------------------------------------------------------------------------------
R17		[7:6] 	PW_LDO_A		PLL analog low drop out regulator switch
0x11							00: off
								01: 2.1V
								10: 2.0V
								11: 1.9V
		[5:3]					cp_cur
								101: 0.2, 111: auto
		[2:0]					011
------------------------------------------------------------------------------------
R18		[7:5] 					set VCO current
0x12	[4]		PW_SDM			0
		[3:0]					000
------------------------------------------------------------------------------------
R19		[7:6]					00
		[5:0]	VER_NUM			0x31
------------------------------------------------------------------------------------
R20		[7:6] 	SI2C			PLL integer divider number input Si2c
0x14							Nint=4*Ni2c+Si2c+13
								PLL divider number Ndiv = (Nint + Nfra)*2
		[5:0] 	NI2C			PLL integer divider number input Ni2c
------------------------------------------------------------------------------------
R21		[7:0] 	SDM_IN[8:1]		PLL fractional divider number input SDM[16:1]
0x15							Nfra=SDM_IN[16]*2^-1+SDM_IN[15]*2^-2+...
R22		[7:0] 	SDM_IN[16:9]	+SDM_IN[2]*2^-15+SDM_IN[1]*2^-16
0x16
------------------------------------------------------------------------------------
R23		[7:6] 	PW_LDO_D		PLL digital low drop out regulator supply current switch
0x17							00: 1.8V,8mA
								01: 1.8V,4mA
								10: 2.0V,8mA
								11: OFF
		[5:4]					div_buf_cur
								10: 200u, 11: 150u
		[3] 	OPEN_D			Open drain
								0: High-Z, 1: Low-Z
		[2:0]					100
------------------------------------------------------------------------------------
R25		[7] 	PWD_RFFILT		RF Filter power
0x19							0: off, 1:on
		[6:5]					RF poly filter current
								00: min
		[4] 	SW_AGC			Switch agc_pin
								0:agc=agc_in
								1:agc=agc_in2
		[3:2]					11
------------------------------------------------------------------------------------
R26		[7:6] 	RFMUX			Tracking Filter switch
0x1A							00: TF on
								01: Bypass
		[5:4]					AGC clk
								00: 300ms, 01: 300ms, 10: 80ms, 11: 20ms
		[3:2]	PLL_AUTO_CLK	PLL auto tune clock rate
								00: 128 kHz
								01: 32 kHz
								10: 8 kHz
		[1:0] RFFILT			RF FILTER band selection
								00: highest band
								01: med band
								10: low band
------------------------------------------------------------------------------------
R27		[7:4] TF_NCH			0000 highest corner for LPNF
0x1B							1111 lowerst corner for LPNF
		[3:0] TF_LP				0000 highest corner for LPF
								1111 lowerst corner for LPF
------------------------------------------------------------------------------------
R28		[7:4]	PDET3_GAIN		Power detector 3 (Mixer) TOP(take off point) control
0x1C							0: Highest, 15: Lowest
		[3]						discharge mode
								0: on
		[2]						1
		[0]						0
------------------------------------------------------------------------------------
R29		[7:6]					11
0x1D	[5:3]	PDET1_GAIN		Power detector 1 (LNA) TOP(take off point) control
								0: Highest, 7: Lowest
		[2:0] 	PDET2_GAIN		Power detector 2 TOP(take off point) control
								0: Highest, 7: Lowest
------------------------------------------------------------------------------------
R30		[7]						0
0x1E 	[6]		FILTER_EXT		Filter extension under weak signal
								0: Disable, 1: Enable
		[5:0]	PDET_CLK		Power detector timing control (LNA discharge current)
	 							111111: max, 000000: min
------------------------------------------------------------------------------------
R31		[7]						Loop through attenuation
0x1F							0: Enable, 1: Disable
		[6:2]					10000
------------------------------------------------------------------------------------
R0...R4 read, R5...R15 read/write, R16..R31 write
*/

/*
 * Static constants
 */

/* Those initial values start from REG_SHADOW_START */
static const uint8_t r82xx_init_array[] = {
	0x80,	//Reg 0x05
	0x12, 	//Reg 0x06
	0x70,	//Reg 0x07
	0xc0, 	//Reg 0x08
	0x40, 	//Reg 0x09
	0xdb, 	//Reg 0x0a
	0x6b,	//Reg 0x0b
	0xf0, 	//Reg 0x0c
	0x53, 	//Reg 0x0d
	0x75, 	//Reg 0x0e
	0x68,	//Reg 0x0f
	0x6c, 	//Reg 0x10
	0xbb, 	//Reg 0x11
	0x80, 	//Reg 0x12
	VER_NUM & 0x3f,	//Reg 0x13
	0x0f, 	//Reg 0x14
	0x00, 	//Reg 0x15
	0xc0, 	//Reg 0x16
	0x30,	//Reg 0x17
	0x48, 	//Reg 0x18
	0xec, 	//Reg 0x19
	0x60, 	//Reg 0x1a
	0x00,	//Reg 0x1b
	0x24,	//Reg 0x1c
	0xdd, 	//Reg 0x1d
	0x0e, 	//Reg 0x1e
	0x40	//Reg 0x1f
};

/* Tuner frequency ranges */
static const struct r82xx_freq_range freq_ranges[] = {
	{
	/* .freq = */			0,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0xdf,	/* R27[7:0]  band2,band0 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			50,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0xbe,	/* R27[7:0]  band4,band1  */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			55,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x8b,	/* R27[7:0]  band7,band4 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			60,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x7b,	/* R27[7:0]  band8,band4 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			65,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x69,	/* R27[7:0]  band9,band6 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			70,	/* Start freq, in MHz */
	/* .open_d = */			0x08,	/* low */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x58,	/* R27[7:0]  band10,band7 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			75,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x44,	/* R27[7:0]  band11,band11 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			80,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x44,	/* R27[7:0]  band11,band11 */
	/* .xtal_cap20p = */	0x02,	/* R16[1:0]  20pF (10)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			90,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x34,	/* R27[7:0]  band12,band11 */
	/* .xtal_cap20p = */	0x01,	/* R16[1:0]  10pF (01)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			100,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x34,	/* R27[7:0]  band12,band11 */
	/* .xtal_cap20p = */	0x01,	/* R16[1:0]  10pF (01)    */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			110,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x24,	/* R27[7:0]  band13,band11 */
	/* .xtal_cap20p = */	0x01,	/* R16[1:0]  10pF (01)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			120,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x24,	/* R27[7:0]  band13,band11 */
	/* .xtal_cap20p = */	0x01,	/* R16[1:0]  10pF (01)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			140,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x14,	/* R27[7:0]  band14,band11 */
	/* .xtal_cap20p = */	0x01,	/* R16[1:0]  10pF (01)   */
	/* .xtal_cap10p = */	0x01,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			180,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x13,	/* R27[7:0]  band14,band12 */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			220,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x13,	/* R27[7:0]  band14,band12 */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			250,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x11,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			280,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x02,	/* R26[7:6]=0 (LPF)  R26[1:0]=2 (low) */
	/* .tf_c = */			0x00,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			310,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x41,	/* R26[7:6]=1 (bypass)  R26[1:0]=1 (middle) */
	/* .tf_c = */			0x00,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			450,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x41,	/* R26[7:6]=1 (bypass)  R26[1:0]=1 (middle) */
	/* .tf_c = */			0x00,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			588,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x40,	/* R26[7:6]=1 (bypass)  R26[1:0]=0 (highest) */
	/* .tf_c = */			0x00,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}, {
	/* .freq = */			650,	/* Start freq, in MHz */
	/* .open_d = */			0x00,	/* high */
	/* .rf_mux_ploy = */	0x40,	/* R26[7:6]=1 (bypass)  R26[1:0]=0 (highest) */
	/* .tf_c = */			0x00,	/* R27[7:0]  highest,highest */
	/* .xtal_cap20p = */	0x00,	/* R16[1:0]  0pF (00)   */
	/* .xtal_cap10p = */	0x00,
	/* .xtal_cap0p = */		0x00,
	}
};

/*
 * I2C read/write code and shadow registers logic
 */
static void shadow_store(struct r82xx_priv *priv, uint8_t reg, const uint8_t *val,
			 int len)
{
	int r = reg - REG_SHADOW_START;

	if (r < 0) {
		len += r;
		r = 0;
	}
	if (len <= 0)
		return;
	if (len > NUM_REGS - r)
		len = NUM_REGS - r;

	memcpy(&priv->regs[r], val, len);
}

static int r82xx_write(struct r82xx_priv *priv, uint8_t reg, const uint8_t *val,
			   unsigned int len)
{
	int rc, size, pos = 0;

	/* Store the shadow registers */
	shadow_store(priv, reg, val, len);

	do {
		if (len > priv->cfg->max_i2c_msg_len - 1)
			size = priv->cfg->max_i2c_msg_len - 1;
		else
			size = len;

		/* Fill I2C buffer */
		priv->buf[0] = reg;
		memcpy(&priv->buf[1], &val[pos], size);

		rc = rtlsdr_i2c_write_fn(priv->rtl_dev, priv->cfg->i2c_addr,
					 priv->buf, size + 1);

		if (rc != size + 1) {
			fprintf(stderr, "%s: i2c wr failed=%d reg=%02x len=%d\n",
				   __FUNCTION__, rc, reg, size);
			if (rc < 0)
				return rc;
			return -1;
		}

		reg += size;
		len -= size;
		pos += size;
	} while (len > 0);

	return 0;
}

static int r82xx_write_reg(struct r82xx_priv *priv, uint8_t reg, uint8_t val)
{
	return r82xx_write(priv, reg, &val, 1);
}

static int r82xx_read_cache_reg(struct r82xx_priv *priv, int reg)
{
	reg -= REG_SHADOW_START;

	if (reg >= 0 && reg < NUM_REGS)
		return priv->regs[reg];
	else
		return -1;
}

static int r82xx_write_reg_mask(struct r82xx_priv *priv, uint8_t reg, uint8_t val,
				uint8_t bit_mask)
{
	int rc = r82xx_read_cache_reg(priv, reg);

	if (rc < 0)
		return rc;

	val = (rc & ~bit_mask) | (val & bit_mask);

	return r82xx_write(priv, reg, &val, 1);
}


static uint8_t r82xx_bitrev(uint8_t byte)
{
	const uint8_t lut[16] = { 0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
				  0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf };

	return (lut[byte & 0xf] << 4) | lut[byte >> 4];
}

static int r82xx_read(struct r82xx_priv *priv, uint8_t *val, int len)
{
	int rc, i;
	uint8_t *p = &priv->buf[0];

	//up to 16 registers can be read
	rc = rtlsdr_i2c_read_fn(priv->rtl_dev, priv->cfg->i2c_addr, p, len);

	if (rc != len) {
		fprintf(stderr, "%s: i2c rd failed=%d len=%d\n",
			   __FUNCTION__, rc, len);
		if (rc < 0)
			return rc;
		return -1;
	}

	/* Copy data to the output buffer */
	for (i = 0; i < len; i++)
		val[i] = r82xx_bitrev(p[i]);

	return 0;
}


static void print_registers(struct r82xx_priv *priv)
{
	uint8_t data[5];
	int rc;
	unsigned int i;

	rc = r82xx_read(priv, data, sizeof(data));
	if (rc < 0)
		return;
	for(i=0; i<sizeof(data); i++)
		printf("%02x ", data[i]);
	printf("\n");
	for(i=sizeof(data); i<32; i++)
		printf("%02x ", r82xx_read_cache_reg(priv, i));
	printf("\n");
}


/*
 * r82xx tuning logic
 */
static int r82xx_set_mux(struct r82xx_priv *priv, uint32_t freq)
{
	const struct r82xx_freq_range *range;
	int rc;
	unsigned int i;
	uint8_t val;

	/* Get the proper frequency range */
	freq = freq / 1000000;
	for (i = 0; i < ARRAY_SIZE(freq_ranges) - 1; i++) {
		if (freq < freq_ranges[i + 1].freq)
			break;
	}
	range = &freq_ranges[i];

	/* Open Drain */
	rc = r82xx_write_reg_mask(priv, 0x17, range->open_d, 0x08);
	if (rc < 0)
		return rc;

	/* RF_MUX,Polymux */
	rc = r82xx_write_reg_mask(priv, 0x1a, range->rf_mux_ploy, 0xc3);
	if (rc < 0)
		return rc;

	/* TF BAND */
	rc = r82xx_write_reg(priv, 0x1b, range->tf_c);
	if (rc < 0)
		return rc;

	/* XTAL CAP & Drive */
	switch (priv->xtal_cap_sel) {
	case XTAL_LOW_CAP_30P:
	case XTAL_LOW_CAP_20P:
		val = range->xtal_cap20p | 0x08;
		break;
	case XTAL_LOW_CAP_10P:
		val = range->xtal_cap10p | 0x08;
		break;
	case XTAL_HIGH_CAP_0P:
		val = range->xtal_cap0p | 0x00;
		break;
	default:
	case XTAL_LOW_CAP_0P:
		val = range->xtal_cap0p | 0x08;
		break;
	}
	rc = r82xx_write_reg_mask(priv, 0x10, val, 0x0b);

	return rc;
}

static int r82xx_set_pll(struct r82xx_priv *priv, uint32_t freq)
{
	int rc, i;
	uint64_t vco_freq;
	uint64_t vco_div;
	uint32_t vco_min = 1770000; /* kHz */
	uint32_t vco_max = vco_min * 2; /* kHz */
	uint32_t freq_khz, pll_ref;
	uint32_t sdm = 0;
	uint8_t mix_div = 2;
	uint8_t div_buf = 0;
	uint8_t div_num = 0;
	uint8_t vco_power_ref = 2;
	uint8_t refdiv2 = 0;
	uint8_t ni, si, nint, vco_fine_tune, val;
	uint8_t data[5];

	/* Frequency in kHz */
	freq_khz = (freq + 500) / 1000;
	pll_ref = priv->cfg->xtal;

	rc = r82xx_write_reg_mask(priv, 0x10, refdiv2, 0x10);
	if (rc < 0)
		return rc;

	/* set pll autotune = 128kHz */
	rc = r82xx_write_reg_mask(priv, 0x1a, 0x00, 0x0c);
	if (rc < 0)
		return rc;

	/* set VCO current = 100 */
	rc = r82xx_write_reg_mask(priv, 0x12, 0x80, 0xe0);
	if (rc < 0)
		return rc;

	/* Calculate divider */
	while (mix_div <= 64) {
		if (((freq_khz * mix_div) >= vco_min) &&
		   ((freq_khz * mix_div) < vco_max)) {
			div_buf = mix_div;
			while (div_buf > 2) {
				div_buf = div_buf >> 1;
				div_num++;
			}
			break;
		}
		mix_div = mix_div << 1;
	}

	rc = r82xx_read(priv, data, sizeof(data));
	if (rc < 0)
		return rc;

	if (priv->cfg->rafael_chip == CHIP_R828D)
		vco_power_ref = 1;

	vco_fine_tune = (data[4] & 0x30) >> 4;

	if (vco_fine_tune > vco_power_ref)
		div_num = div_num - 1;
	else if (vco_fine_tune < vco_power_ref)
		div_num = div_num + 1;

	rc = r82xx_write_reg_mask(priv, 0x10, div_num << 5, 0xe0);
	if (rc < 0)
		return rc;

	vco_freq = (uint64_t)freq * (uint64_t)mix_div;

	/*
	 * We want to approximate:
	 *
	 *  vco_freq / (2 * pll_ref)
	 *
	 * in the form:
	 *
	 *  nint + sdm/65536
	 *
	 * where nint,sdm are integers and 0 < nint, 0 <= sdm < 65536
	 *
	 * Scaling to fixed point and rounding:
	 *
	 *  vco_div = 65536*(nint + sdm/65536) = int( 0.5 + 65536 * vco_freq / (2 * pll_ref) )
	 *  vco_div = 65536*nint + sdm         = int( (pll_ref + 65536 * vco_freq) / (2 * pll_ref) )
	 */

	vco_div = (pll_ref + 65536 * vco_freq) / (2 * pll_ref);
        nint = (uint32_t) (vco_div / 65536);
	sdm = (uint32_t) (vco_div % 65536);

#if 0
	{
	  uint64_t actual_vco = (uint64_t)2 * pll_ref * nint + (uint64_t)2 * pll_ref * sdm / 65536;
	  fprintf(stderr, "[R82XX] requested %uHz; selected mix_div=%u vco_freq=%lu nint=%u sdm=%u; actual_vco=%lu; tuning error=%+dHz\n",
		  freq, mix_div, vco_freq, nint, sdm, actual_vco, (int32_t) (actual_vco - vco_freq) / mix_div);
	}
#endif

	if (nint > ((128 / vco_power_ref) - 1)) {
		fprintf(stderr, "[R82XX] No valid PLL values for %u Hz!\n", freq);
		return -1;
	}

	ni = (nint - 13) / 4;
	si = nint - 4 * ni - 13;

	rc = r82xx_write_reg(priv, 0x14, ni + (si << 6));
	if (rc < 0)
		return rc;

	/* pw_sdm */
	if (sdm == 0)
		val = 0x08;
	else
		val = 0x00;

	rc = r82xx_write_reg_mask(priv, 0x12, val, 0x08);
	if (rc < 0)
		return rc;

	rc = r82xx_write_reg(priv, 0x16, sdm >> 8);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x15, sdm & 0xff);
	if (rc < 0)
		return rc;

	for (i = 0; i < 2; i++) {

		/* Check if PLL has locked */
		rc = r82xx_read(priv, data, 3);
		if (rc < 0)
			return rc;
		if (data[2] & 0x40)
			break;

		if (!i) {
			/* Didn't lock. Increase VCO current */
			rc = r82xx_write_reg_mask(priv, 0x12, 0x60, 0xe0);
			if (rc < 0)
				return rc;
		}
	}

	if (!(data[2] & 0x40)) {
		fprintf(stderr, "[R82XX] PLL not locked for %u Hz!\n", freq);
		priv->has_lock = 0;
		return 0;
	}

	priv->has_lock = 1;

	/* set pll autotune = 8kHz */
	rc = r82xx_write_reg_mask(priv, 0x1a, 0x08, 0x08);

	return rc;
}

static int r82xx_sysfreq_sel(struct r82xx_priv *priv,
				 enum r82xx_tuner_type type)
{
	int rc;

	uint8_t lna_top = 0xe5;		/* detect bw 3, lna top:4, predet top:2 */
	uint8_t pre_dect = 0x40;
	uint8_t air_cable1_in = 0x00;
	uint8_t cable2_in = 0x00;

	if (priv->cfg->use_predetect) {
		rc = r82xx_write_reg_mask(priv, 0x06, pre_dect, 0x40);
		if (rc < 0)
			return rc;
	}

	priv->input = air_cable1_in;

	/* Air-IN only for Astrometa */
	rc = r82xx_write_reg_mask(priv, 0x05, air_cable1_in, 0x60);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg_mask(priv, 0x06, cable2_in, 0x08);
	if (rc < 0)
		return rc;

	/*
	 * Set LNA
	 */

	if (type != TUNER_ANALOG_TV) {
		/* LNA TOP: lowest */
		rc = r82xx_write_reg_mask(priv, 0x1d, 0, 0x38);
		if (rc < 0)
			return rc;

		/* 0: PRE_DECT off */
		rc = r82xx_write_reg_mask(priv, 0x06, 0, 0x40);
		if (rc < 0)
			return rc;

		/* agc clk 250hz */
		rc = r82xx_write_reg_mask(priv, 0x1a, 0x30, 0x30);
		if (rc < 0)
			return rc;

		/* write LNA TOP = 3 */
		rc = r82xx_write_reg_mask(priv, 0x1d, 0x18, 0x38);
		if (rc < 0)
			return rc;

		/* agc clk 60hz */
		rc = r82xx_write_reg_mask(priv, 0x1a, 0x20, 0x30);
		if (rc < 0)
			return rc;
	} else {
		/* PRE_DECT off */
		rc = r82xx_write_reg_mask(priv, 0x06, 0, 0x40);
		if (rc < 0)
			return rc;

		/* write LNA TOP */
		rc = r82xx_write_reg_mask(priv, 0x1d, lna_top, 0x38);
		if (rc < 0)
			return rc;

		/* agc clk 1Khz, external det1 cap 1u */
		rc = r82xx_write_reg_mask(priv, 0x1a, 0x00, 0x30);
		if (rc < 0)
			return rc;

		rc = r82xx_write_reg_mask(priv, 0x10, 0x00, 0x04);
		if (rc < 0)
			return rc;
	 }
	 return 0;
}

static int r82xx_set_tv_standard(struct r82xx_priv *priv,
				 enum r82xx_tuner_type type,
				 uint32_t delsys)

{
	int rc, i;
	uint8_t data[5];

	int need_calibration = 1;

	/* BW < 6 MHz */
	uint32_t filt_cal_lo = 56000;	/* 52000->56000 */
	uint8_t filt_q = 0x10;		/* r10[4]:low q(1'b1) */

	/* for LT Gain test */
	if (type != TUNER_ANALOG_TV) {
		rc = r82xx_write_reg_mask(priv, 0x1d, 0x00, 0x38);
		if (rc < 0)
			return rc;
	}
	priv->int_freq = 3570 * 1000;
	priv->sideband = 0;

	/* Check if standard changed. If so, filter calibration is needed */
	/* as we call this function only once in rtlsdr, force calibration */

	if (need_calibration) {
		for (i = 0; i < 2; i++) {

			/* set cali clk =on */
			rc = r82xx_write_reg_mask(priv, 0x0f, 0x04, 0x04);
			if (rc < 0)
				return rc;

			rc = r82xx_set_pll(priv, filt_cal_lo * 1000);
			if (rc < 0 || !priv->has_lock)
				return rc;

			/* Start Trigger */
			rc = r82xx_write_reg_mask(priv, 0x0b, 0x10, 0x10);
			if (rc < 0)
				return rc;

			/* Stop Trigger */
			rc = r82xx_write_reg_mask(priv, 0x0b, 0x00, 0x10);
			if (rc < 0)
				return rc;

			/* set cali clk =off */
			rc = r82xx_write_reg_mask(priv, 0x0f, 0x00, 0x04);
			if (rc < 0)
				return rc;

			/* Check if calibration worked */
			rc = r82xx_read(priv, data, sizeof(data));
			if (rc < 0)
				return rc;

			priv->fil_cal_code = data[4] & 0x0f;
			if (priv->fil_cal_code && priv->fil_cal_code != 0x0f)
				break;
		}
		/* narrowest */
		if (priv->fil_cal_code == 0x0f)
			priv->fil_cal_code = 0;
	}

	rc = r82xx_write_reg_mask(priv, 0x0a,
				  filt_q | priv->fil_cal_code, 0x1f);
	if (rc < 0)
		return rc;

	/* Store current standard. If it changes, re-calibrate the tuner */
	priv->delsys = delsys;
	priv->type = type;
	priv->bw = 3;

	return 0;
}


/* measured with a Racal 6103E GSM test set at 928 MHz with -60 dBm
 * input power, for raw results see:
 * http://steve-m.de/projects/rtl-sdr/gain_measurement/r820t/
 */

#define VGA_BASE_GAIN	-47
static const int r82xx_vga_gain_steps[]  = {
	0, 36, 34, 34, 36, 36, 42, 34, 36, 38, 34, 32, 38, 34, 32, 40
};

static const int r82xx_lna_gain_steps[]  = {
	0, 9, 13, 40, 38, 13, 31, 22, 26, 31, 26, 14, 19, 5, 35, 13
};

static const int r82xx_mixer_gain_steps[]  = {
	0, 5, 10, 10, 19, 9, 10, 25, 17, 10, 8, 16, 13, 6, 3, -8
};

int r82xx_set_gain(struct r82xx_priv *priv, int set_manual_gain, int gain)
{
	int rc;

	int i, total_gain = 0;
	uint8_t vga_index = 0;

	if (set_manual_gain) {
		/* set VGA gain */
		for (i = 0; i < 15; i++) {
			if (total_gain >= gain)
				break;
			total_gain += r82xx_vga_gain_steps[++vga_index];
		}
		rc = r82xx_write_reg_mask(priv, 0x0c, vga_index, 0x1f); //IF vga gain controlled by vga_code[3:0]
	} else
		rc = r82xx_write_reg_mask(priv, 0x0c, 0x10, 0x1f); //IF vga gain controlled by vagc pin
	if (rc < 0)
		return rc;
	//print_registers(priv);
	return 0;
}


/* expose/permit tuner specific i2c register hacking! */
int r82xx_set_i2c_register(struct r82xx_priv *priv, unsigned i2c_register, unsigned data, unsigned mask)
{
	uint8_t reg = i2c_register & 0xFF;
	uint8_t reg_mask = mask & 0xFF;
	uint8_t reg_val = data & 0xFF;
	return r82xx_write_reg_mask(priv, reg, reg_val, reg_mask);
}

//-cs-
int r82xx_get_i2c_register(struct r82xx_priv *priv, unsigned char* data, int len)
{
	int rc, i, len1;

	// The lower 16 I2C registers can be read with the normal read fct, the upper ones are read from the cache
	if(len < 5)
		len1 = len;
	else
		len1 = 5;
	rc = r82xx_read(priv, data, len1);
	if (rc < 0)
		return rc;
	if(len > 5)
		for (i = 5; i < len; i++)
			data[i] = r82xx_read_cache_reg(priv, i);
	return 0;
}
//-cs- end

static const int r82xx_bws[]=     {  300,  450,  600,  900, 1100, 1200, 1300, 1500, 1800, 2200, 3000, 5000 };
static const uint8_t r82xx_0xa[]= { 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0e, 0x0f, 0x0f, 0x04, 0x0b };
static const uint8_t r82xx_0xb[]= { 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xaf, 0x8f, 0x8f, 0x6b };
static const int r82xx_if[]  =    { 1700, 1650, 1600, 1500, 1400, 1350, 1320, 1270, 1400, 1600, 2000, 3570 };

int r82xx_set_bandwidth(struct r82xx_priv *priv, int bw, uint32_t * applied_bw, int apply)
{
	int rc;
	unsigned int i;

	for(i=0; i < ARRAY_SIZE(r82xx_bws) - 1; ++i) {
		/* bandwidth is compared to median of the current and next available bandwidth in the table */
		if (bw < (r82xx_bws[i+1] + r82xx_bws[i]) * 500)
			break;
	}
	*applied_bw = r82xx_bws[i] * 1000;
	if (apply)
		priv->int_freq = r82xx_if[i] * 1000;
	else
		return 0;

	/* Register 0xA = R10 */
	rc = r82xx_write_reg_mask(priv, 0x0a, r82xx_0xa[i], 0x0f);
	if (rc < 0)
		return rc;

	/* Register 0xB = R11 with undocumented Bit 7 for filter bandwidth for Hi-part FILT_BW */
	rc = r82xx_write_reg_mask(priv, 0x0b, r82xx_0xb[i], 0xef);
	if (rc < 0)
		return rc;

	return priv->int_freq;
   //print_registers(priv);
}

int r82xx_set_sideband(struct r82xx_priv *priv, int sideband)
{
	int rc;
	priv->sideband = sideband;
	rc = r82xx_write_reg_mask(priv, 0x07, (sideband << 7) & 0x80, 0x80);
	if (rc < 0)
		return rc;
	return 0;
}

int r82xx_set_freq(struct r82xx_priv *priv, uint32_t freq)
{
	int rc = -1;
	uint32_t lo_freq;
	uint8_t air_cable1_in;

	if(priv->sideband)
		lo_freq = freq - priv->int_freq;
	else
		lo_freq = freq + priv->int_freq;
	rc = r82xx_set_mux(priv, lo_freq);
	if (rc < 0)
		goto err;

	rc = r82xx_set_pll(priv, lo_freq);
	if (rc < 0 || !priv->has_lock)
		goto err;

	/* switch between 'Cable1' and 'Air-In' inputs on sticks with
	 * R828D tuner. We switch at 345 MHz, because that's where the
	 * noise-floor has about the same level with identical LNA
	 * settings. The original driver used 320 MHz. */
	air_cable1_in = (freq > MHZ(345)) ? 0x00 : 0x60;

	if ((priv->cfg->rafael_chip == CHIP_R828D) &&
		(air_cable1_in != priv->input)) {
		priv->input = air_cable1_in;
		rc = r82xx_write_reg_mask(priv, 0x05, air_cable1_in, 0x60);
	}

err:
	if (rc < 0)
		fprintf(stderr, "%s: failed=%d\n", __FUNCTION__, rc);
	return rc;
}

/*
 * r82xx standby logic
 */

int r82xx_standby(struct r82xx_priv *priv)
{
	int rc;

	/* If device was not initialized yet, don't need to standby */
	if (!priv->init_done)
		return 0;

	rc = r82xx_write_reg(priv, 0x06, 0xb1);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x05, 0xa0);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x07, 0x3a);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x08, 0x40);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x09, 0xc0);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x0a, 0x36);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x0c, 0x35);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x0f, 0x68);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x11, 0x03);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x17, 0xf4);
	if (rc < 0)
		return rc;
	rc = r82xx_write_reg(priv, 0x19, 0x0c);

	/* Force initial calibration */
	priv->type = -1;

	return rc;
}

/*
 * r82xx device init logic
 */
int r82xx_init(struct r82xx_priv *priv)
{
	int rc;

	/* TODO: R828D might need r82xx_xtal_check() */
	priv->xtal_cap_sel = XTAL_HIGH_CAP_0P;

	/* Initialize registers */
	rc = r82xx_write(priv, 0x05, r82xx_init_array, sizeof(r82xx_init_array));

	rc = r82xx_set_tv_standard(priv, TUNER_DIGITAL_TV, 0);
	if (rc < 0)
		goto err;

	rc = r82xx_sysfreq_sel(priv, TUNER_DIGITAL_TV);

	priv->init_done = 1;

err:
	if (rc < 0)
		fprintf(stderr, "%s: failed=%d\n", __FUNCTION__, rc);
	return rc;
}
