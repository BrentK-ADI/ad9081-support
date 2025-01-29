/*
 * Example application to fully configure the AD9081 from libiio based on
 * settings found using the IIO-Scope plugin.
 *
 * Copyright © 2024 by Analog Devices, Inc.  All rights reserved.
 * This software is proprietary to Analog Devices, Inc. and its licensors.
 * This software is provided on an “as is” basis without any representations,
 * warranties, guarantees or liability of any kind.
 * Use of the software is subject to the terms and conditions of the
 * Clear BSD License ( https://spdx.org/licenses/BSD-3-Clause-Clear.html ).
 *
 * Author: Brent Kowal <brent.kowal@analog.com>
 */
#include <iio.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>

/* Some reporting helpers */
#define ARGS(fmt, ...)	__VA_ARGS__
#define FMT(fmt, ...)	fmt
#define error(...) \
	printf("%s, %d: ERROR: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

#define info(...) \
	printf("%s, %d: INFO: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))



/* The AD9081 has a configurable number of channels based on the JESD and
 * device setup.
 * Unless otherwise provided at build time, assume the application is working
 * with 8 pairs of I&Q for both input and output
 */
#ifndef NUM_CH
#define NUM_CH	8
#endif


/* Length of temporary buffers for holding channel names */
#define TEMP_BUFF_LEN	64

/* Structure for holding channel pairs when dealing with I & Q components */
typedef struct {
	struct iio_channel* ch_i;
	struct iio_channel* ch_q;
} iio_channel_pair_t;

/* Structure for holding channels for the DDS tone generation */
typedef struct {
	iio_channel_pair_t tone1;
	iio_channel_pair_t tone2;
} dds_channels_t;

/* Structure for holding default Rx configuration values */
typedef struct {
	long long nco_freq_hz;			/* (FDDC/Channel) NCO Freq in Hz */
	long long nco_phase_mdeg;   	/* (FDDC/Channel) NCO Phase in mDeg */
	long long main_nco_freq_hz; 	/* (CDDC/Main) NCO Freq in Hz */
	long long main_nco_phase_mdeg; 	/* (CDDC/MAIN) NCO Phase in mDeg */
} rx_default_config_t;

/* Structure for holding default Tx configuration values */
typedef struct {
	bool enabled;
	double gain_scale;				/* Channel Gain Scale */
	long long nco_freq_hz;			/* (FDUC/Channel) NCO Freq in HZ */
	long long nco_phase_mdeg; 		/* (FDUC/Channel) NCO Phase in mDeg */
	long long main_nco_freq_hz;		/* (CDUC/Main) NCO Freq in Hz */
	long long main_nco_phase_mdeg;	/* (CDUC/Main) NCO Phase in mDeg */
} tx_default_config_t;

/* Structure for holding default DDS configuration values.
 * The engine support independent I&Q x2 tones, but for this example, we're
 * using a single tone, automatically adjusting the phase between I&Q
 */
typedef struct {
	bool enabled;		/* DDS Tone generator enabled */
	long long freq_hz;	/* Tone Frequency */
	double phase_deg;	/* Tone phase in Deg */
	double scale_dbfs;	/* Tone gain/scale in dB. -Inf-0 are valid */
} dds_default_config_t;

static iio_channel_pair_t ad9081_inputs[NUM_CH];
static iio_channel_pair_t ad9081_outputs[NUM_CH];
static dds_channels_t     ad9081_dds[NUM_CH];

static struct iio_context *ctx = NULL;

/* For the AD9081, the Rx device serves as both the Rx device, and the device
 * for configuring all the channels (rx & tx)
 * The Tx device manages the DDS/DMA for transmit
 */
static struct iio_device* ad9081_cfg_rx = NULL;
static struct iio_device* ad9081_tx = NULL;

/* Default configuration parameters for the Rx Channels */
static const rx_default_config_t rx_default_configs[] = {
	{.nco_freq_hz = 10000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 100000000LL, .main_nco_phase_mdeg = 1000LL },

	{.nco_freq_hz = 20000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 200000000LL, .main_nco_phase_mdeg = 0LL },

	{.nco_freq_hz = 30000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 100000000LL, .main_nco_phase_mdeg = 1000LL },

	{.nco_freq_hz = 40000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 200000000LL, .main_nco_phase_mdeg = 0LL },

	{.nco_freq_hz = 50000000LL, .nco_phase_mdeg = 1000LL,
	 .main_nco_freq_hz = 700000000LL, .main_nco_phase_mdeg = 1000LL },

	{.nco_freq_hz = 60000000LL, .nco_phase_mdeg = 1000LL,
	 .main_nco_freq_hz = 900000000LL, .main_nco_phase_mdeg = 1000LL },

	{.nco_freq_hz = 70000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 700000000LL, .main_nco_phase_mdeg = 1000LL },

	{.nco_freq_hz = 80000000LL, .nco_phase_mdeg = 1000LL,
	 .main_nco_freq_hz = 900000000LL, .main_nco_phase_mdeg = 1000LL }};

/* Default configuration parameters for the Tx Channels */
static const tx_default_config_t tx_default_configs[] = {
	{.enabled = true, .gain_scale = 1.0,
	 .nco_freq_hz = 6000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 100000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.7001221,
	 .nco_freq_hz = 16000000LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 100000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.5699633,
	 .nco_freq_hz = 0LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 100000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.5001221,
	 .nco_freq_hz = 100000000LL, .nco_phase_mdeg = 2000LL,
	 .main_nco_freq_hz = 400000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.5001221,
	 .nco_freq_hz = 0LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 700000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.5001221,
	 .nco_freq_hz = 0LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 700000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.5001221,
	 .nco_freq_hz = 0LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 900000000LL, .main_nco_phase_mdeg = 0LL },

	{.enabled = true, .gain_scale = 0.48009768,
	 .nco_freq_hz = 0LL, .nco_phase_mdeg = 0LL,
	 .main_nco_freq_hz = 900000000LL, .main_nco_phase_mdeg = 0LL },
};

/* Default DDS configuration parameters */
static const dds_default_config_t dds_default_configs[] = {
	{ .enabled = true, .freq_hz = 4018290LL,  .phase_deg = 90.0, .scale_dbfs = -12.0},
	{ .enabled = true, .freq_hz = 8005900LL,  .phase_deg = 90.0, .scale_dbfs = -10.0},
	{ .enabled = true, .freq_hz = 3000940LL,  .phase_deg = 90.0, .scale_dbfs = -13.0},
	{ .enabled = false },
	{ .enabled = true, .freq_hz = 10996668LL, .phase_deg = 90.0, .scale_dbfs = -17.0},
	{ .enabled = true, .freq_hz = 6012054LL,  .phase_deg = 90.0, .scale_dbfs = -15.0},
	{ .enabled = true, .freq_hz = 11993591LL, .phase_deg = 90.0, .scale_dbfs = -14.0},
	{ .enabled = true, .freq_hz = 12990513LL, .phase_deg = 90.0, .scale_dbfs = -13.0},
};

/**
 * Helper to convert dB to linear value for DDS tone scale
 * Note: This does not check the range of the db input. The tone generator
 * accepts 0-1.0 linear (-inf-0 dB)
 */
static inline double dbfs_to_linear(double db)
{
	return pow(10, db/20.0);
}

/**
 *  Helper to normalize an angle (in degrees) to 0-360
 */
static inline double normalize_degrees(double degs)
{
	while( degs >= 360.0 ) { degs -= 360.0; }
	while( degs < 0.0 ) { degs += 360.0; }
	return degs;
}

/**
 * Helper function to load all the channels. Since the both Tx and Rx
 * configuration channels have the same name, do things in parallel.
 */
static int load_channels(void)
{
	int i;
	int result = 0; //Assume successful
	char ch_buff[TEMP_BUFF_LEN];

	for(i = 0; i < NUM_CH; i++) {
		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_i", i);
		if((ad9081_inputs[i].ch_i = iio_device_find_channel(ad9081_cfg_rx, ch_buff, false)) == NULL) {
			result = -1;
			error("Error finding Input Channel %s\n",ch_buff);
		}
		if((ad9081_outputs[i].ch_i = iio_device_find_channel(ad9081_cfg_rx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_q", i);
		if((ad9081_inputs[i].ch_q = iio_device_find_channel(ad9081_cfg_rx, ch_buff, false)) == NULL) {
			result = -1;
			error("Error finding Input Channel %s\n",ch_buff);
		}
		if((ad9081_outputs[i].ch_q = iio_device_find_channel(ad9081_cfg_rx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output Channel %s\n",ch_buff);
		}

		/* Tone indexes start at 1, not 0.  The DDS engines support independent
		 * I & Q signal generation on each output channel, for 2 tones, for a
		 * total of 4 IIO channels each.
		 */
		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_I_F1", i+1);
		if((ad9081_dds[i].tone1.ch_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 1 I Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_Q_F1", i+1);
		if((ad9081_dds[i].tone1.ch_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 1 Q Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_I_F2", i+1);
		if((ad9081_dds[i].tone2.ch_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 2 I Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_Q_F2", i+1);
		if((ad9081_dds[i].tone2.ch_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 2 Q Channel %s\n",ch_buff);
		}
	}

	return result;
}

/**
 * Loads the provided DDS channel set based on the tone_configuration. The DDS
 * engine support 2x independent tones for both I and Q. This example just
 * assumes a single tone, and I->Q phase automatic to 90
 */
static void load_dds_tone(dds_channels_t* dds_ch, const dds_default_config_t* tone_config)
{
	/* Only 1 tone being used. Always set tone 2 scale to 0 to "disable" it */
	if(iio_channel_attr_write_double(dds_ch->tone2.ch_i, "scale", 0.0) < 0) {
		error("Error setting Tone 2 I scale to 0\n");
	}
	if(iio_channel_attr_write_double(dds_ch->tone2.ch_q, "scale", 0.0) < 0) {
		error("Error setting Tone 2 Q scale to 0\n");
	}

	/* If the tone is enabled, configure everything */
	if(tone_config->enabled) {
		if(iio_channel_attr_write_longlong(dds_ch->tone1.ch_i, "frequency",
				tone_config->freq_hz) < 0) {
			error("Error setting Tone 1 I frequency\n");
		}
		if(iio_channel_attr_write_longlong(dds_ch->tone1.ch_q, "frequency",
				tone_config->freq_hz) < 0) {
			error("Error setting Tone 1 Q frequency\n");
		}
		if(iio_channel_attr_write_double(dds_ch->tone1.ch_i, "scale",
				dbfs_to_linear(tone_config->scale_dbfs)) < 0) {
			error("Error setting Tone 1 I scale\n");
		}
		if(iio_channel_attr_write_double(dds_ch->tone1.ch_q, "scale",
				dbfs_to_linear(tone_config->scale_dbfs)) < 0) {
			error("Error setting Tone 1 Q scale\n");
		}

		/* I component is -90 degrees from the requested phase, scaled up to mDeg*/
		if(iio_channel_attr_write_longlong(dds_ch->tone1.ch_i, "phase",
				(long long)(normalize_degrees(tone_config->phase_deg - 90.0) * 1000.0)) < 0) {
			error("Error setting Tone 1 I phase\n");
		}

		/* Q component is the requested phase, scaled up to mDeg*/
		if(iio_channel_attr_write_longlong(dds_ch->tone1.ch_i, "phase",
				(long long)(normalize_degrees(tone_config->phase_deg) * 1000.0)) < 0) {
			error("Error setting Tone 1 Q phase\n");
		}

		/* Set raw to true to enable the engine */
		if(iio_channel_attr_write_bool(dds_ch->tone1.ch_i, "raw", true) < 0) {
			error("Error setting Tone 1 I raw\n");
		}
		if(iio_channel_attr_write_bool(dds_ch->tone1.ch_q, "raw", true) < 0) {
			error("Error setting Tone 1 Q raw\n");
		}
	}
	else { /* Otherwise, Tone 1 scale is also 0 */
		if(iio_channel_attr_write_double(dds_ch->tone1.ch_i, "scale", 0.0) < 0) {
			error("Error setting Tone 1 I scale to 0\n");
		}
		if(iio_channel_attr_write_double(dds_ch->tone1.ch_q, "scale", 0.0) < 0) {
			error("Error setting Tone 1 Q scale to 0\n");
		}
	}
}


int main(int argc, char* argv[])
{
	int i;
	struct iio_channel* temp_ch;
	const rx_default_config_t* temp_rx_cfg;
	const tx_default_config_t* temp_tx_cfg;

	ctx = iio_create_default_context();
	if (!ctx) {
		error("Could not create IIO context\n");
		return EXIT_FAILURE;
	}

    /* Devices are found by their IIO name */
	ad9081_cfg_rx = iio_context_find_device(ctx, "axi-ad9081-rx-hpc");
	ad9081_tx = iio_context_find_device(ctx, "axi-ad9081-tx-hpc");
	if(!ad9081_cfg_rx || !ad9081_tx) {
        error("Could not find AD9081 Devices\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	if(load_channels() < 0) {
		error("Could not find all the channels\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}


	/* Perform the channel configuration per the setup */
	/* Global Rx. These will apply to all channels even though just 1 is poked */
	if(iio_channel_attr_write(ad9081_inputs[0].ch_i, "test_mode", "off") < 0) {
		error("Error writing RX Test mode\n");
	}

	if(iio_channel_attr_write(ad9081_inputs[0].ch_i, "nyquist_zone", "odd") < 0) {
		error("Error writing RX Nyquist Zone\n");
	}

	if(iio_device_attr_write_longlong(ad9081_cfg_rx, "loopback_mode", 0) < 0) {
		error("Error writing Loopback mode\n");
	}

	/* Configure all the input channels based on the default Rx configuration */
	for( i = 0; i < NUM_CH; i++) {
		/* Attributes will get applied to both I & Q via I*/
		temp_ch = ad9081_inputs[i].ch_i;
		temp_rx_cfg = &rx_default_configs[i];

		if(iio_channel_attr_write_longlong(temp_ch, "main_nco_frequency",
				temp_rx_cfg->main_nco_freq_hz) < 0) {
			error("Error writing Rx Main NCO Frequency on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "main_nco_phase",
				temp_rx_cfg->main_nco_phase_mdeg) < 0) {
			error("Error writing Rx Main NCO Phase on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "channel_nco_frequency",
				temp_rx_cfg->nco_freq_hz) < 0) {
			error("Error writing Rx Channel NCO Frequency on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "channel_nco_phase",
				temp_rx_cfg->nco_phase_mdeg) < 0) {
			error("Error writing Rx Channel NCO Phase on %d\n", i);
		}
	}

	/* Configure all the output channels based on the default Tx configuration */
	for( i = 0; i < NUM_CH; i++) {
		/* Attributes will get applied to both I & Q via I*/
		temp_ch = ad9081_outputs[i].ch_i;
		temp_tx_cfg = &tx_default_configs[i];

		if(iio_channel_attr_write_longlong(temp_ch, "main_nco_frequency",
				temp_tx_cfg->main_nco_freq_hz) < 0) {
			error("Error writing Tx Main NCO Frequency on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "main_nco_phase",
				temp_tx_cfg->main_nco_phase_mdeg) < 0) {
			error("Error writing Tx Main NCO Phase on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "channel_nco_frequency",
				temp_tx_cfg->nco_freq_hz) < 0) {
			error("Error writing Tx Channel NCO Frequency on %d\n", i);
		}
		if(iio_channel_attr_write_longlong(temp_ch, "channel_nco_phase",
				temp_tx_cfg->nco_phase_mdeg) < 0) {
			error("Error writing Tx Channel NCO Phase on %d\n", i);
		}
		if(iio_channel_attr_write_double(temp_ch, "channel_nco_gain_scale",
				temp_tx_cfg->gain_scale) < 0) {
			error("Error writing Tx Gain Scale on %d\n", i);
		}
		if(iio_channel_attr_write_bool(temp_ch, "en", temp_tx_cfg->enabled) < 0) {
			error("Error writing Tx Enable on %d\n", i);
		}
	}

	/* Configure all the DDS engines */
	for( i = 0; i < NUM_CH; i++) {
		load_dds_tone(&ad9081_dds[i], &dds_default_configs[i]);
	}

	/*** Do your useful work here! ****/

	/* Clean up and exit */
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
