#include <windows.h>
#include <airspy.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C"
{

typedef	int(*pfn_airspy_init) (void);

#define GETPROCADDRESS GetProcAddress

pfn_airspy_lib_version airspy_lib_version;
pfn_airspy_init		   airspy_init;
pfn_airspy_exit		   airspy_exit;
pfn_airspy_error_name	   airspy_error_name;
pfn_airspy_open		   airspy_open;
pfn_airspy_close	   airspy_close;
pfn_airspy_get_samplerates airspy_get_samplerates;
pfn_airspy_set_samplerate  airspy_set_samplerate;
pfn_airspy_start_rx	   airspy_start_rx;
pfn_airspy_stop_rx	   airspy_stop_rx;
pfn_airspy_set_sample_type airspy_set_sample_type;
pfn_airspy_set_freq	   airspy_set_freq;
pfn_airspy_set_lna_gain	   airspy_set_lna_gain;
pfn_airspy_set_mixer_gain  airspy_set_mixer_gain;
pfn_airspy_set_vga_gain	   airspy_set_vga_gain;
pfn_airspy_set_linearity_gain airspy_set_linearity_gain;
pfn_airspy_set_sensitivity_gain airspy_set_sensitivity_gain;
pfn_airspy_set_lna_agc	   airspy_set_lna_agc;
pfn_airspy_set_mixer_agc   airspy_set_mixer_agc;
pfn_airspy_set_rf_bias	   airspy_set_rf_bias;
pfn_airspy_board_id_read   airspy_board_id_read;
pfn_airspy_board_id_name   airspy_board_id_name;
pfn_airspy_board_partid_serialno_read airspy_board_partid_serialno_read;
pfn_airspy_set_packing airspy_set_packing;
pfn_airspy_r820t_write airspy_r820t_write;
pfn_airspy_r820t_read airspy_r820t_read;


	bool	load_airspyFunctions(HMODULE h) {

		airspy_lib_version = (pfn_airspy_lib_version)GETPROCADDRESS(h, "airspy_lib_version");
		if (airspy_lib_version == 0) {
			fprintf(stderr, "Could not find airspy_lib_version\n");
			return false;
		}

		airspy_init = (pfn_airspy_init)GETPROCADDRESS(h, "airspy_init");
		if (airspy_init == 0) {
			fprintf(stderr, "Could not find airspy_init\n");
			return false;
		}

		airspy_exit = (pfn_airspy_exit)
			GETPROCADDRESS(h, "airspy_exit");
		if (airspy_exit == 0) {
			fprintf(stderr, "Could not find airspy_exit\n");
			return false;
		}

		airspy_open = (pfn_airspy_open)
			GETPROCADDRESS(h, "airspy_open");
		if (airspy_open == 0) {
			fprintf(stderr, "Could not find airspy_open\n");
			return false;
		}

		airspy_close = (pfn_airspy_close)
			GETPROCADDRESS(h, "airspy_close");
		if (airspy_close == 0) {
			fprintf(stderr, "Could not find airspy_close\n");
			return false;
		}

		airspy_get_samplerates = (pfn_airspy_get_samplerates)
			GETPROCADDRESS(h, "airspy_get_samplerates");
		if (airspy_get_samplerates == 0) {
			fprintf(stderr, "Could not find airspy_get_samplerates\n");
			return false;
		}

		airspy_set_samplerate = (pfn_airspy_set_samplerate)
			GETPROCADDRESS(h, "airspy_set_samplerate");
		if (airspy_set_samplerate == 0) {
			fprintf(stderr, "Could not find airspy_set_samplerate\n");
			return false;
		}

		airspy_start_rx = (pfn_airspy_start_rx)
			GETPROCADDRESS(h, "airspy_start_rx");
		if (airspy_start_rx == 0) {
			fprintf(stderr, "Could not find airspy_start_rx\n");
			return false;
		}

		airspy_stop_rx = (pfn_airspy_stop_rx)
			GETPROCADDRESS(h, "airspy_stop_rx");
		if (airspy_stop_rx == 0) {
			fprintf(stderr, "Could not find airspy_stop_rx\n");
			return false;
		}

		airspy_set_sample_type = (pfn_airspy_set_sample_type)
			GETPROCADDRESS(h, "airspy_set_sample_type");
		if (airspy_set_sample_type == 0) {
			fprintf(stderr, "Could not find airspy_set_sample_type\n");
			return false;
		}

		airspy_set_freq = (pfn_airspy_set_freq)
			GETPROCADDRESS(h, "airspy_set_freq");
		if (airspy_set_freq == 0) {
			fprintf(stderr, "Could not find airspy_set_freq\n");
			return false;
		}

		airspy_set_lna_gain = (pfn_airspy_set_lna_gain)
			GETPROCADDRESS(h, "airspy_set_lna_gain");
		if (airspy_set_lna_gain == 0) {
			fprintf(stderr, "Could not find airspy_set_lna_gain\n");
			return false;
		}

		airspy_set_mixer_gain = (pfn_airspy_set_mixer_gain)
			GETPROCADDRESS(h, "airspy_set_mixer_gain");
		if (airspy_set_mixer_gain == 0) {
			fprintf(stderr, "Could not find airspy_set_mixer_gain\n");
			return false;
		}

		airspy_set_vga_gain = (pfn_airspy_set_vga_gain)
			GETPROCADDRESS(h, "airspy_set_vga_gain");
		if (airspy_set_vga_gain == 0) {
			fprintf(stderr, "Could not find airspy_set_vga_gain\n");
			return false;
		}

		airspy_set_linearity_gain = (pfn_airspy_set_linearity_gain)
			GETPROCADDRESS(h, "airspy_set_linearity_gain");
		if (airspy_set_linearity_gain == 0) {
			fprintf(stderr, "Could not find airspy_set_linearity_gain\n");
			fprintf(stderr, "You probably did install an old library\n");
			return false;
		}

		airspy_set_sensitivity_gain = (pfn_airspy_set_sensitivity_gain)
			GETPROCADDRESS(h, "airspy_set_sensitivity_gain");
		if (airspy_set_sensitivity_gain == 0) {
			fprintf(stderr, "Could not find airspy_set_sensitivity_gain\n");
			fprintf(stderr, "You probably did install an old library\n");
			return false;
		}

		airspy_set_lna_agc = (pfn_airspy_set_lna_agc)
			GETPROCADDRESS(h, "airspy_set_lna_agc");
		if (airspy_set_lna_agc == 0) {
			fprintf(stderr, "Could not find airspy_set_lna_agc\n");
			return false;
		}

		airspy_set_mixer_agc = (pfn_airspy_set_mixer_agc)
			GETPROCADDRESS(h, "airspy_set_mixer_agc");
		if (airspy_set_mixer_agc == 0) {
			fprintf(stderr, "Could not find airspy_set_mixer_agc\n");
			return false;
		}

		airspy_set_rf_bias = (pfn_airspy_set_rf_bias)
			GETPROCADDRESS(h, "airspy_set_rf_bias");
		if (airspy_set_rf_bias == 0) {
			fprintf(stderr, "Could not find airspy_set_rf_bias\n");
			return false;
		}

		airspy_error_name = (pfn_airspy_error_name)
			GETPROCADDRESS(h, "airspy_error_name");
		if (airspy_error_name == 0) {
			fprintf(stderr, "Could not find airspy_error_name\n");
			return false;
		}

		airspy_board_id_read = (pfn_airspy_board_id_read)
			GETPROCADDRESS(h, "airspy_board_id_read");
		if (airspy_board_id_read == 0) {
			fprintf(stderr, "Could not find airspy_board_id_read\n");
			return false;
		}

		airspy_board_id_name = (pfn_airspy_board_id_name)
			GETPROCADDRESS(h, "airspy_board_id_name");
		if (airspy_board_id_name == 0) {
			fprintf(stderr, "Could not find airspy_board_id_name\n");
			return false;
		}

		airspy_board_partid_serialno_read =
			(pfn_airspy_board_partid_serialno_read)
			GETPROCADDRESS(h, "airspy_board_partid_serialno_read");
		if (airspy_board_partid_serialno_read == 0) {
			fprintf(stderr, "Could not find airspy_board_partid_serialno_read\n");
			return false;
		}

		airspy_set_packing =
			(pfn_airspy_set_packing)
			GETPROCADDRESS(h, "airspy_set_packing");
		if (airspy_set_packing == 0) {
			fprintf(stderr, "Could not find airspy_set_packing\n");
			return false;
		}

		airspy_r820t_read =
			(pfn_airspy_r820t_read)
			GETPROCADDRESS(h, "airspy_r820t_read");
		if (airspy_r820t_read == 0) {
			fprintf(stderr, "Could not find airspy_r820t_read\n");
			return false;
		}
		airspy_r820t_write =
			(pfn_airspy_r820t_write)
			GETPROCADDRESS(h, "airspy_r820t_write");
		if (airspy_r820t_write == 0) {
			fprintf(stderr, "Could not find airspy_r820t_write\n");
			return false;
		}

		return true;
	}
}
