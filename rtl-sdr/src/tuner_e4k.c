/*
 * Elonics E4000 tuner driver
 *
 * (C) 2011-2012 by Harald Welte <laforge@gnumonks.org>
 * (C) 2012 by Sylvain Munaut <tnt@246tNt.com>
 * (C) 2012 by Hoernchen <la@tfc-server.de>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <tuner_e4k.h>
#include <rtlsdr_i2c.h>


/* If this is defined, the limits are somewhat relaxed compared to what the
 * vendor claims is possible */
#define OUT_OF_SPEC

#define MHZ(x)	((x)*1000*1000)
#define KHZ(x)	((x)*1000)

uint32_t unsigned_delta(uint32_t a, uint32_t b)
{
	if (a > b)
		return a - b;
	else
		return b - a;
}

/* look-up table bit-width -> mask */
static const uint8_t width2mask[] = {
	0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff
};

/***********************************************************************
 * Register Access */

static int e4k_write_array(struct e4k_state *e4k, uint8_t reg, const uint8_t *val,
			   		unsigned int len)
{
	uint8_t buf[MAX_I2C_MSG_LEN];
	int rc, size, pos = 0;

	do {
		if (len > MAX_I2C_MSG_LEN - 1)
			size = MAX_I2C_MSG_LEN - 1;
		else
			size = len;

		/* Fill I2C buffer */
		buf[0] = reg;
		memcpy(&buf[1], &val[pos], size);

		rc = rtlsdr_i2c_write_fn(e4k->rtl_dev, e4k->i2c_addr, buf, size + 1);
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

/*! \brief Write a register of the tuner chip
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \param[in] val value to be written
 *  \returns 0 on success, negative in case of error
 */
static int e4k_reg_write(struct e4k_state *e4k, uint8_t reg, uint8_t val)
{
	return e4k_write_array(e4k, reg, &val, 1);
}

static int e4k_read_array(struct e4k_state *e4k, uint8_t reg, uint8_t *val, unsigned int len)
{
	int rc, size, pos = 0;

	do {
		if (len > MAX_I2C_MSG_LEN)
			size = MAX_I2C_MSG_LEN;
		else
			size = len;

		rc = rtlsdr_i2c_write_fn(e4k->rtl_dev, e4k->i2c_addr, &reg, 1);
		if (rc < 1)
			return rc;
		rc = rtlsdr_i2c_read_fn(e4k->rtl_dev, e4k->i2c_addr, val+pos, size);
		if (rc != size) {
			fprintf(stderr, "%s: i2c rd failed=%d reg=%02x len=%d\n",
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

/*! \brief Read a register of the tuner chip
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \returns positive 8bit register contents on success, negative in case of error
 */
static int e4k_reg_read(struct e4k_state *e4k, uint8_t reg)
{
	uint8_t data = reg;

	if (rtlsdr_i2c_write_fn(e4k->rtl_dev, e4k->i2c_addr, &data, 1) < 1)
		return -1;

	if (rtlsdr_i2c_read_fn(e4k->rtl_dev, e4k->i2c_addr, &data, 1) < 1)
		return -1;

	return data;
}

/*! \brief Set or clear some (masked) bits inside a register
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \param[in] mask bit-mask of the value
 *  \param[in] val data value to be written to register
 *  \returns 0 on success, negative in case of error
 */
static int e4k_reg_set_mask(struct e4k_state *e4k, uint8_t reg,
		     uint8_t mask, uint8_t val)
{
	uint8_t tmp = e4k_reg_read(e4k, reg);

	if ((tmp & mask) == val)
		return 0;

	return e4k_reg_write(e4k, reg, (tmp & ~mask) | (val & mask));
}

/***********************************************************************
 * Filter Control */

static const uint32_t rf_filt_center_uhf[] = {
	MHZ(360), MHZ(380), MHZ(405), MHZ(425),
	MHZ(450), MHZ(475), MHZ(505), MHZ(540),
	MHZ(575), MHZ(615), MHZ(670), MHZ(720),
	MHZ(760), MHZ(840), MHZ(890), MHZ(970)
};

static const uint32_t rf_filt_center_l[] = {
	MHZ(1300), MHZ(1320), MHZ(1360), MHZ(1410),
	MHZ(1445), MHZ(1460), MHZ(1490), MHZ(1530),
	MHZ(1560), MHZ(1590), MHZ(1640), MHZ(1660),
	MHZ(1680), MHZ(1700), MHZ(1720), MHZ(1750)
};

static int closest_arr_idx(const uint32_t *arr, unsigned int arr_size, uint32_t freq)
{
	unsigned int i, bi = 0;
	uint32_t best_delta = 0xffffffff;

	/* iterate over the array containing a list of the center
	 * frequencies, selecting the closest one */
	for (i = 0; i < arr_size; i++) {
		uint32_t delta = unsigned_delta(freq, arr[i]);
		if (delta < best_delta) {
			best_delta = delta;
			bi = i;
		}
	}

	return bi;
}

/* return 4-bit index as to which RF filter to select */
static int choose_rf_filter(enum e4k_band band, uint32_t freq)
{
	int rc;

	switch (band) {
		case E4K_BAND_VHF2:
		case E4K_BAND_VHF3:
			rc = 0;
			break;
		case E4K_BAND_UHF:
			rc = closest_arr_idx(rf_filt_center_uhf,
						 ARRAY_SIZE(rf_filt_center_uhf),
						 freq);
			break;
		case E4K_BAND_L:
			rc = closest_arr_idx(rf_filt_center_l,
						 ARRAY_SIZE(rf_filt_center_l),
						 freq);
			break;
		default:
			rc = -EINVAL;
			break;
	}

	return rc;
}

/* \brief Automatically select apropriate RF filter based on e4k state */
static int e4k_rf_filter_set(struct e4k_state *e4k)
{
	int rc;

	rc = choose_rf_filter(e4k->band, e4k->vco.flo);
	if (rc < 0)
		return rc;

	return e4k_reg_set_mask(e4k, E4K_REG_FILT1, 0xF, rc);
}

int e4k_set_bandwidth(struct e4k_state *e4k, int bw, uint32_t *applied_bw, int apply)
{
	uint8_t data[2];

	if (bw < 2200000)
	{
		*applied_bw = 2000000;
		data[0] = 0xff;
	}
	else if (bw < 3000000)
	{
		*applied_bw = 2400000;
		data[0] = 0xfe;
	}
	else if (bw < 3950000)
	{
		*applied_bw = 3600000;
		data[0] = 0xfd;
	}
	else
	{
		*applied_bw = 4300000;
		data[0] = 0xfc;
	}
	if(!apply)
		return 0;

	/* Mixer Filter 1900 kHz (0.2 dB Bandwidth) */
	/* IF RC Filter = 2000, 2400, 3600 or 5200 kHz */
	/* IF Channel Filter 4300 kHz */
	data[1] = 0x1f;
	return e4k_write_array(e4k, E4K_REG_FILT2, data, 2);
}


/***********************************************************************
 * Frequency Control */

#define E4K_FVCO_MIN_KHZ	2600000	/* 2.6 GHz */
#define E4K_FVCO_MAX_KHZ	3900000	/* 3.9 GHz */
#define E4K_PLL_Y			65536

#ifdef OUT_OF_SPEC
#define E4K_FLO_MIN_MHZ		50
#define E4K_FLO_MAX_MHZ		2200UL
#else
#define E4K_FLO_MIN_MHZ		64
#define E4K_FLO_MAX_MHZ		1700
#endif

struct pll_settings {
	uint32_t freq;
	uint8_t reg_synth7;
	uint8_t mult;
};

static const struct pll_settings pll_vars[] = {
	{KHZ(72400),	(1 << 3) | 7,	48},
	{KHZ(81200),	(1 << 3) | 6,	40},
	{KHZ(108300),	(1 << 3) | 5,	32},
	{KHZ(162500),	(1 << 3) | 4,	24},
	{KHZ(216600),	(1 << 3) | 3,	16},
	{KHZ(325000),	(1 << 3) | 2,	12},
	{KHZ(350000),	(1 << 3) | 1,	8},
	{KHZ(432000),	(0 << 3) | 3,	8},
	{KHZ(667000),	(0 << 3) | 2,	6},
	{KHZ(1200000),	(0 << 3) | 1,	4}
};

static int is_fvco_valid(uint32_t fvco_z)
{
	/* check if the resulting fosc is valid */
	if (fvco_z/1000 < E4K_FVCO_MIN_KHZ ||
	    fvco_z/1000 > E4K_FVCO_MAX_KHZ) {
		fprintf(stderr, "[E4K] Fvco %u invalid\n", fvco_z);
		return 0;
	}

	return 1;
}

static int is_fosc_valid(uint32_t fosc)
{
	if (fosc < MHZ(16) || fosc > MHZ(30)) {
		fprintf(stderr, "[E4K] Fosc %u invalid\n", fosc);
		return 0;
	}

	return 1;
}

/*! \brief Determine if 3-phase mixing shall be used or not */
static int use_3ph_mixing(uint32_t flo)
{
	/* this is a magic number somewhre between VHF and UHF */
	if (flo < MHZ(350))
		return 1;

	return 0;
}

/* \brief compute Fvco based on Fosc, Z and X
 * \returns positive value (Fvco in Hz), 0 in case of error */
static uint64_t compute_fvco(uint32_t f_osc, uint8_t z, uint16_t x)
{
	uint64_t fvco_z, fvco_x, fvco;

	/* We use the following transformation in order to
	 * handle the fractional part with integer arithmetic:
	 *  Fvco = Fosc * (Z + X/Y) <=> Fvco = Fosc * Z + (Fosc * X)/Y
	 * This avoids X/Y = 0.  However, then we would overflow a 32bit
	 * integer, as we cannot hold e.g. 26 MHz * 65536 either.
	 */
	fvco_z = (uint64_t)f_osc * z;

#if 0
	if (!is_fvco_valid(fvco_z))
		return 0;
#endif

	fvco_x = ((uint64_t)f_osc * x) / E4K_PLL_Y;

	fvco = fvco_z + fvco_x;

	return fvco;
}

static uint32_t compute_flo(uint32_t f_osc, uint8_t z, uint16_t x, uint8_t r)
{
	uint64_t fvco = compute_fvco(f_osc, z, x);
	if (fvco == 0)
		return -EINVAL;

	return fvco / r;
}

static int e4k_band_set(struct e4k_state *e4k, enum e4k_band band)
{
	int rc;

	switch (band) {
	case E4K_BAND_VHF2:
	case E4K_BAND_VHF3:
	case E4K_BAND_UHF:
		e4k_reg_write(e4k, E4K_REG_BIAS, 3);
		break;
	case E4K_BAND_L:
		e4k_reg_write(e4k, E4K_REG_BIAS, 0);
		break;
	}

	/* workaround: if we don't reset this register before writing to it,
	 * we get a gap between 325-350 MHz */
	rc = e4k_reg_set_mask(e4k, E4K_REG_SYNTH1, 0x06, 0);
	rc = e4k_reg_set_mask(e4k, E4K_REG_SYNTH1, 0x06, band << 1);
	if (rc >= 0)
		e4k->band = band;

	return rc;
}

/*! \brief Compute PLL parameters for givent target frequency
 *  \param[out] oscp Oscillator parameters, if computation successful
 *  \param[in] fosc Clock input frequency applied to the chip (Hz)
 *  \param[in] intended_flo target tuning frequency (Hz)
 *  \returns actual PLL frequency, as close as possible to intended_flo,
 *	     0 in case of error
 */
static uint32_t e4k_compute_pll_params(struct e4k_pll_params *oscp, uint32_t fosc, uint32_t intended_flo)
{
	uint32_t i;
	uint8_t r = 2;
	uint64_t intended_fvco, remainder;
	uint64_t z = 0;
	uint32_t x;
	int flo;
	int three_phase_mixing = 0;
	oscp->r_idx = 0;

	if (!is_fosc_valid(fosc))
		return 0;

	for(i = 0; i < ARRAY_SIZE(pll_vars); ++i) {
		if(intended_flo < pll_vars[i].freq) {
			three_phase_mixing = (pll_vars[i].reg_synth7 & 0x08) ? 1 : 0;
			oscp->r_idx = pll_vars[i].reg_synth7;
			r = pll_vars[i].mult;
			break;
		}
	}

	//fprintf(stderr, "[E4K] Fint=%u, R=%u\n", intended_flo, r);

	/* flo(max) = 1700MHz, R(max) = 48, we need 64bit! */
	intended_fvco = (uint64_t)intended_flo * r;

	/* compute integral component of multiplier */
	z = intended_fvco / fosc;

	/* compute fractional part.  this will not overflow,
	* as fosc(max) = 30MHz and z(max) = 255 */
	remainder = intended_fvco - (fosc * z);
	/* remainder(max) = 30MHz, E4K_PLL_Y = 65536 -> 64bit! */
	x = (remainder * E4K_PLL_Y) / fosc;
	/* x(max) as result of this computation is 65536 */

	flo = compute_flo(fosc, z, x, r);

	oscp->fosc = fosc;
	oscp->flo = flo;
	oscp->intended_flo = intended_flo;
	oscp->r = r;
//	oscp->r_idx = pll_vars[i].reg_synth7 & 0x0;
	oscp->threephase = three_phase_mixing;
	oscp->x = x;
	oscp->z = z;

	return flo;
}

static int e4k_tune_params(struct e4k_state *e4k, struct e4k_pll_params *p)
{
	uint8_t data[3];
	/* program R + 3phase/2phase */
	e4k_reg_write(e4k, E4K_REG_SYNTH7, p->r_idx);
	data[0] = p->z; /* program Z */
	data[1] = p->x & 0xff; /* program X */
	data[2] = p->x >> 8;
	e4k_write_array(e4k, E4K_REG_SYNTH3, data, 3);

	/* we're in auto calibration mode, so there's no need to trigger it */
	memcpy(&e4k->vco, p, sizeof(e4k->vco));

	/* set the band */
	if (e4k->vco.flo < MHZ(140))
		e4k_band_set(e4k, E4K_BAND_VHF2);
	else if (e4k->vco.flo < MHZ(350))
		e4k_band_set(e4k, E4K_BAND_VHF3);
	else if (e4k->vco.flo < MHZ(1135))
		e4k_band_set(e4k, E4K_BAND_UHF);
	else
		e4k_band_set(e4k, E4K_BAND_L);

	/* select and set proper RF filter */
	e4k_rf_filter_set(e4k);

	return e4k->vco.flo;
}

/*! \brief High-level tuning API, just specify frquency
 *
 *  This function will compute matching PLL parameters, program them into the
 *  hardware and set the band as well as RF filter.
 *
 *  \param[in] e4k reference to tuner
 *  \param[in] freq frequency in Hz
 *  \returns actual tuned frequency, negative in case of error
 */
int e4k_tune_freq(struct e4k_state *e4k, uint32_t freq)
{
	uint32_t rc;
	struct e4k_pll_params p;

	/* determine PLL parameters */
	rc = e4k_compute_pll_params(&p, e4k->vco.fosc, freq);
	if (!rc)
		return -EINVAL;

	/* actually tune to those parameters */
	rc = e4k_tune_params(e4k, &p);

	/* check PLL lock */
	rc = e4k_reg_read(e4k, E4K_REG_SYNTH1);
	if (!(rc & 0x01)) {
		fprintf(stderr, "[E4K] PLL not locked for %u Hz!\n", freq);
		return -1;
	}

	return 0;
}

/***********************************************************************
 * Gain Control */

static const int8_t if_stage1_gain[] = {
	-3, 6
};

static const int8_t if_stage23_gain[] = {
	0, 3, 6, 9
};

static const int8_t if_stage4_gain[] = {
	0, 1, 2, 3
};

static const int8_t if_stage56_gain[] = {
	3, 6, 9, 12, 15, 15, 15, 15
};

static const int8_t *if_stage_gain[] = {
	0,
	if_stage1_gain,
	if_stage23_gain,
	if_stage23_gain,
	if_stage4_gain,
	if_stage56_gain,
	if_stage56_gain
};

static const uint8_t if_stage_gain_len[] = {
	0,
	ARRAY_SIZE(if_stage1_gain),
	ARRAY_SIZE(if_stage23_gain),
	ARRAY_SIZE(if_stage23_gain),
	ARRAY_SIZE(if_stage4_gain),
	ARRAY_SIZE(if_stage56_gain),
	ARRAY_SIZE(if_stage56_gain)
};

static const struct reg_field if_stage_gain_regs[] = {
	{ 0, 0, 0 },
	{ E4K_REG_GAIN3, 0, 1 },
	{ E4K_REG_GAIN3, 1, 2 },
	{ E4K_REG_GAIN3, 3, 2 },
	{ E4K_REG_GAIN3, 5, 2 },
	{ E4K_REG_GAIN4, 0, 3 },
	{ E4K_REG_GAIN4, 3, 3 }
};

int e4k_enable_manual_gain(struct e4k_state *e4k, uint8_t manual)
{
	if (manual) {
		/* Set IF mode to manual */
		e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK, E4K_AGC_MOD_IF_SERIAL_LNA_AUTON);

		/* Set Mixer Gain Control to manual */
		e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 0);
	} else {
		/* Set IF mode to auto */
		e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK, E4K_AGC_MOD_IF_DIG_LNA_AUTON);

		/* Set Mixer Gain Control to auto */
		e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 1);
	}
	return 0;
}

static int find_stage_gain(uint8_t stage, int8_t val)
{
	const int8_t *arr;
	int i;

	if (stage >= ARRAY_SIZE(if_stage_gain))
		return -EINVAL;

	arr = if_stage_gain[stage];

	for (i = 0; i < if_stage_gain_len[stage]; i++) {
		if (arr[i] == val)
			return i;
	}
	return -EINVAL;
}

/*! \brief Set the gain of one of the IF gain stages
 *  \param [e4k] handle to the tuner chip
 *  \param [stage] number of the stage (1..6)
 *  \param [value] gain value in dB
 *  \returns 0 on success, negative in case of error
 */
int e4k_if_gain_set(struct e4k_state *e4k, uint8_t stage, int8_t value)
{
	int rc;
	uint8_t mask;
	const struct reg_field *field;

	rc = find_stage_gain(stage, value);
	if (rc < 0)
		return rc;

	/* compute the bit-mask for the given gain field */
	field = &if_stage_gain_regs[stage];
	mask = width2mask[field->width] << field->shift;

	return e4k_reg_set_mask(e4k, field->reg, mask, rc << field->shift);
}

static int e4k_mixer_gain_set(struct e4k_state *e4k, int8_t value)
{
	uint8_t bit;

	switch (value) {
	case 4:
		bit = 0;
		break;
	case 12:
		bit = 1;
		break;
	default:
		return -EINVAL;
	}

	return e4k_reg_set_mask(e4k, E4K_REG_GAIN2, 1, bit);
}

static int e4k_commonmode_set(struct e4k_state *e4k, int8_t value)
{
	if(value < 0)
		return -EINVAL;
	else if(value > 7)
		return -EINVAL;

	return e4k_reg_set_mask(e4k, E4K_REG_DC7, 7, value);
}

/***********************************************************************
 * DC Offset */

static int e4k_manual_dc_offset(struct e4k_state *e4k, int8_t iofs, int8_t irange, int8_t qofs, int8_t qrange)
{
	int res;

	if((iofs < 0x00) || (iofs > 0x3f))
		return -EINVAL;
	if((irange < 0x00) || (irange > 0x03))
		return -EINVAL;
	if((qofs < 0x00) || (qofs > 0x3f))
		return -EINVAL;
	if((qrange < 0x00) || (qrange > 0x03))
		return -EINVAL;

	res = e4k_reg_set_mask(e4k, E4K_REG_DC2, 0x3f, iofs);
	if(res < 0)
		return res;

	res = e4k_reg_set_mask(e4k, E4K_REG_DC3, 0x3f, qofs);
	if(res < 0)
		return res;

	res = e4k_reg_set_mask(e4k, E4K_REG_DC4, 0x33, (qrange << 4) | irange);
	return res;
}

/*! \brief Perform a DC offset calibration right now
 *  \param [e4k] handle to the tuner chip
 */
static int e4k_dc_offset_calibrate(struct e4k_state *e4k)
{
	/* make sure the DC range detector is enabled */
	e4k_reg_set_mask(e4k, E4K_REG_DC5, E4K_DC5_RANGE_DET_EN, E4K_DC5_RANGE_DET_EN);

	return e4k_reg_write(e4k, E4K_REG_DC1, 0x01);
}


static const int8_t if_gains_max[] = {
	0, 6, 9, 9, 2, 15, 15
};

struct gain_comb {
	int8_t mixer_gain;
	int8_t if1_gain;
	uint8_t reg;
};

static const struct gain_comb dc_gain_comb[] = {
	{ 4,  -3, 0x50 },
	{ 4,   6, 0x51 },
	{ 12, -3, 0x52 },
	{ 12,  6, 0x53 },
};

#define TO_LUT(offset, range)	(offset | (range << 6))

static int e4k_dc_offset_gen_table(struct e4k_state *e4k)
{
	uint32_t i;

	/* FIXME: read ont current gain values and write them back
	 * before returning to the caller */

	/* disable auto mixer gain */
	e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 0);

	/* set LNA/IF gain to full manual */
	e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK,
			 E4K_AGC_MOD_SERIAL);

	/* set all 'other' gains to maximum */
	for (i = 2; i <= 6; i++)
		e4k_if_gain_set(e4k, i, if_gains_max[i]);

	/* iterate over all mixer + if_stage_1 gain combinations */
	for (i = 0; i < ARRAY_SIZE(dc_gain_comb); i++) {
		uint8_t offs_i, offs_q, range, range_i, range_q;

		/* set the combination of mixer / if1 gain */
		e4k_mixer_gain_set(e4k, dc_gain_comb[i].mixer_gain);
		e4k_if_gain_set(e4k, 1, dc_gain_comb[i].if1_gain);

		/* perform actual calibration */
		e4k_dc_offset_calibrate(e4k);

		/* extract I/Q offset and range values */
		offs_i = e4k_reg_read(e4k, E4K_REG_DC2) & 0x3f;
		offs_q = e4k_reg_read(e4k, E4K_REG_DC3) & 0x3f;
		range  = e4k_reg_read(e4k, E4K_REG_DC4);
		range_i = range & 0x3;
		range_q = (range >> 4) & 0x3;

		fprintf(stderr, "[E4K] Table %u I=%u/%u, Q=%u/%u\n",
			i, range_i, offs_i, range_q, offs_q);

		/* write into the table */
		e4k_reg_write(e4k, dc_gain_comb[i].reg,
			      TO_LUT(offs_q, range_q));
		e4k_reg_write(e4k, dc_gain_comb[i].reg + 0x10,
			      TO_LUT(offs_i, range_i));
	}

	return 0;
}

/***********************************************************************
 * Standby */

/* Enable/disable standby mode
 */
int e4k_standby(struct e4k_state *e4k, int enable)
{
	e4k_reg_set_mask(e4k, E4K_REG_MASTER1, E4K_MASTER1_NORM_STBY,
			 enable ? 0 : E4K_MASTER1_NORM_STBY);
	return 0;
}

/***********************************************************************
 * Initialization */

int e4k_init(struct e4k_state *e4k)
{
	uint8_t data[3];

	/* make a dummy i2c read or write command, will not be ACKed! */
	e4k_reg_read(e4k, 0);

	/* Make sure we reset everything and clear POR indicator */
	e4k_reg_write(e4k, E4K_REG_MASTER1,
		E4K_MASTER1_RESET |
		E4K_MASTER1_NORM_STBY |
		E4K_MASTER1_POR_DET
	);

	/* Configure clock input */
	e4k_reg_write(e4k, E4K_REG_CLK_INP, 0x00);

	/* Disable clock output */
	e4k_reg_write(e4k, E4K_REG_REF_CLK, 0x00);
	e4k_reg_write(e4k, E4K_REG_CLKOUT_PWDN, 0x96);

	/* Write some magic values into registers */
	data[0] = 0x01;
	data[1] = 0xfe;
	e4k_write_array(e4k, 0x7e, data, 2);
	e4k_reg_write(e4k, 0x82, 0x00);
	data[0] = 0x51; /* polarity B */
	data[1] = 0x20;
	data[2] = 0x01;
	e4k_write_array(e4k, 0x86, data, 3);
	data[0] = 0x7f;
	data[1] = 0x07;
	e4k_write_array(e4k, 0x9f, data, 2);

#if 0
	/* Set common mode voltage a bit higher for more margin 850 mv */
	e4k_commonmode_set(e4k, 4);

	/* Initialize DC offset lookup tables */
	e4k_dc_offset_gen_table(e4k);

	/* Enable time variant DC correction */
	data[0] = 0x01;
	data[1] = 0x01;
	e4k_write_array(e4k, E4K_REG_DCTIME1, data, 2);
#endif

	/* Set LNA mode to manual */
	data[0] = 32;   /* High threshold */
	data[1] = 14;	/* Low threshold */
	data[2] = 0x18;	/* LNA calib + loop rate */
	e4k_write_array(e4k, E4K_REG_AGC4, data, 3);

	/* Set Mixer Gain Control to auto */
	e4k_reg_write(e4k, E4K_REG_AGC7, 0x15);

	/* Enable LNA Gain enhancement */
	e4k_reg_set_mask(e4k, E4K_REG_AGC11, 0x7,
			 E4K_AGC11_LNA_GAIN_ENH | (2 << 1));

	/* Enable automatic IF gain mode switching */
	e4k_reg_set_mask(e4k, E4K_REG_AGC8, 0x1, E4K_AGC8_SENS_LIN_AUTO);

	/* Use auto-gain as default */
	e4k_enable_manual_gain(e4k, 0);

	/* Set the most narrow filter we can possibly use */
	data[0] = 0xff;
	data[1] = 0x1f;
	e4k_write_array(e4k, E4K_REG_FILT2, data, 2);

	/* Disable time variant DC correction and LUT */
	e4k_reg_set_mask(e4k, E4K_REG_DC5, 0x03, 0);
	e4k_reg_set_mask(e4k, E4K_REG_DCTIME1, 0x03, 0);
	e4k_reg_set_mask(e4k, E4K_REG_DCTIME2, 0x03, 0);

	return 0;
}
//sensitivity mode
static const uint8_t e4k_reg21[] = {0,  0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1};
static const uint8_t e4k_reg22[] = {0,  2,   4,0x20,0x22,0x24,0x21,0x23,0x25,0x27,0x2f,0x37,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x7f};
static const uint8_t e4k_reg23[] = {0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   2,   3,   4,0x0c,0x14,0x1c,0x24,0x24};

/*
//linearity mode
static const uint8_t e4k_reg21[] = {0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1};
static const uint8_t e4k_reg22[] = {0,  0,   0,   0,   0,   0,   0,   0,   0,   8,0x10,0x18,0x1a,0x1c,0x19,0x1b,0x1d,0x1f,0x7f,0x3d,0x3f,0x7f};
static const uint8_t e4k_reg23[] = {0,  8,0x10,0x18,0x20,0x21,0x22,0x23,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24};
*/
/* all gain values are expressed in tenths of a dB */
static const int     e4k_gains[] = {0, 30,  60,  90, 120, 150, 180, 210, 240, 270, 300, 330, 360, 390, 420, 450, 480, 510, 540, 570, 600, 620};

#define GAIN_CNT	(sizeof(e4k_gains) / sizeof(int))

int e4k_set_gain(struct e4k_state *e4k, int gain)
{
	uint8_t data[3];
	unsigned int i;

	for (i = 0; i < GAIN_CNT; i++)
		if ((e4k_gains[i] >= gain) || (i+1 == GAIN_CNT))
			break;
	data[0] = e4k_reg21[i];
	data[1] = e4k_reg22[i];
	data[2] = e4k_reg23[i];
	return e4k_write_array(e4k, E4K_REG_GAIN2, data, 3);
}

const int *e4k_get_gains(int *len)
{
	*len = sizeof(e4k_gains);
	return e4k_gains;
}

static const int lna_gain_table[] = {
	-50, -25, -50, -25, 0, 25, 50, 75, 100, 125, 150, 175, 200, 250, 300, 300
};
static const int mixer_gain_table[] = {
	40, 120
};

static int e4k_get_signal_strength(uint8_t *data)
{
	int lna_gain = lna_gain_table[data[0x14] & 0xf];
	int mixer_gain = mixer_gain_table[data[0x15] & 1];
	int if_gain = if_stage1_gain[data[0x16] & 1];
	if_gain += if_stage23_gain[(data[0x16] >> 1) & 3];
	if_gain += if_stage23_gain[(data[0x16] >> 3) & 3];
	if_gain += if_stage4_gain[(data[0x16] >> 5) & 3];
	if_gain += if_stage56_gain[data[0x17] & 7];
	if_gain += if_stage56_gain[(data[0x16] >> 3) & 7];
	if_gain *= 10;
	return 990 - if_gain - mixer_gain - lna_gain;
}

int e4k_set_i2c_register(struct e4k_state *e4k, unsigned i2c_register, unsigned data, unsigned mask)
{
	return e4k_reg_set_mask(e4k, i2c_register & 0xFF, mask & 0xff, data & 0xff);
}

int e4k_get_i2c_register(struct e4k_state *e4k, uint8_t *data, int *len, int *strength)
{
	int rc;

	*len = 168;
	*strength = 0;
	rc = e4k_read_array(e4k, 0, data, *len);
	if (rc < 0)
		return rc;
	*strength = e4k_get_signal_strength(data);
	return 0;
}

