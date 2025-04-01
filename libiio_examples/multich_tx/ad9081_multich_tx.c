/*
 * Example application showing Tx on multiple channels and the impact of the
 * underlying DAC/TPL Registers
 *
 * Copyright © 2025 by Analog Devices, Inc.  All rights reserved.
 * This software is proprietary to Analog Devices, Inc. and its licensors.
 * This software is provided on an “as is” basis without any representations,
 * warranties, guarantees or liability of any kind.
 * Use of the software is subject to the terms and conditions of the
 * Clear BSD License ( https://spdx.org/licenses/BSD-3-Clause-Clear.html ).
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
#include <unistd.h>

/* The AD9081 has a configurable number of channels based on the JESD and
 * device setup.
 * Unless otherwise provided at build time, assume the application is working
 * with 8 pairs of I&Q for both input and output
 */
#ifndef NUM_TX_CH
#define NUM_TX_CH   8
#endif

/* Length of temporary buffers for holding channel names */
#define TEMP_BUFF_LEN	64

/* Constants for direct register access for channel information */
#define DAC_CH_REG_BASE		0x400
#define DAC_CH_REG_STEP		0x40
#define DAC_CH_CTRL_OFFSET	0x18

#define ARGS(fmt, ...)	__VA_ARGS__
#define FMT(fmt, ...)	fmt
#define error(...) \
	printf("%s, %d: ERROR: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

#define info(...) \
	printf("%s, %d: INFO: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

/* Helper struct for holding channel pair information. The config_ channels are
 * from the ad9081 'PHY' and used for configuring things like the NCO freq.
 * The dac_ channels are from the DAC/DMA block and are use for setting up the
 * data buffers
 *
 * For test purposes, the software just generates a linear ramp, setup by the
 * remaining parameters
 */
typedef struct {
	struct iio_channel *config_i;
	struct iio_channel *config_q;
	struct iio_channel *dac_i;
	struct iio_channel *dac_q;
	uint16_t ramp_i;
	uint16_t ramp_q;
	uint16_t ramp_inc;
} tx_channel_pair_t;

/* Holds all the channel pairs */
static tx_channel_pair_t tx_channels[NUM_TX_CH];

static bool stop_loop = false;

static struct iio_context *ctx = NULL;
static struct iio_device *ad9081 = NULL;
static struct iio_device *ad9081_tx = NULL;

/**
 * Handle keyboard interrupts to gracefully exit.
 */
static void handle_sig(int sig)
{
	stop_loop = true;
}

/**
 * Helper function to inspect and print register values from the DAC engine for
 * each channel
 */
static void inspect_dac_regs(void)
{
	int i;
	uint32_t reg_val;
	uint32_t reg_addr;
	printf("**DAC Regs**\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		reg_addr = DAC_CH_REG_BASE + (i * DAC_CH_REG_STEP) + DAC_CH_CTRL_OFFSET;
		iio_device_reg_read(ad9081_tx, reg_addr, &reg_val);
		printf("Ch %d: CTRL7 (0x%X) = 0x%02X\n", i, reg_addr, reg_val);
	}
	printf("\n\n");
}

/**
 * Loads the Tx channels into the tx_channels array.  Loads both the config
 * channels from the ad9081 'PHY', and the buffer channels from the DAC
 */
static int load_channels(void)
{
	int i;
	int result = 0; //Assume successful
	char ch_buff[TEMP_BUFF_LEN];

	for(i = 0; i < NUM_TX_CH; i++) {
		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_i", i);
		if((tx_channels[i].config_i = iio_device_find_channel(ad9081, ch_buff, false)) == NULL) {
			result = -1;
			error("Error finding Output (Config) Channel %s\n",ch_buff);
		}
		if((tx_channels[i].dac_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output (DAC) Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_q", i);
		if((tx_channels[i].config_q = iio_device_find_channel(ad9081, ch_buff, false)) == NULL) {
			result = -1;
			error("Error finding Output (Config) Channel %s\n",ch_buff);
		}
		if((tx_channels[i].dac_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output (DAC) Channel %s\n",ch_buff);
		}

		/* Initialize the dummy ramp value */
		tx_channels[i].ramp_i = 0x0;
		tx_channels[i].ramp_q = 0x8000;
		tx_channels[i].ramp_inc = 1 << i; //Different ramp rate for each pair
	}

	return result;
}


int main(int argc, char* argv[])
{
	int ret;
	int result;
	int i;
	uint16_t* p_dat, *p_end;

	struct iio_channel *dac1_dds_ctrl = NULL;
	struct iio_buffer  *sample_buff = NULL;

	signal(SIGINT, handle_sig);

	ctx = iio_create_default_context();
	if (!ctx) {
		error("Could not create IIO context\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	//Devices are found by their IIO name
	ad9081_tx = iio_context_find_device(ctx, "axi-ad9081-tx-hpc");
	ad9081 = iio_context_find_device(ctx, "axi-ad9081-rx-hpc");
	if (!ad9081 || !ad9081_tx) {
		error("Could not find AD9081 Device\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	//Load the Tx channels
	info("Loading Channels\n");
	result = load_channels();

	//We need to find just 1 DDS control channel which has a global effect on
	//all DAC channels when the raw parameter is set
	dac1_dds_ctrl = iio_device_find_channel(ad9081_tx, "altvoltage0", true);
	if ((!dac1_dds_ctrl) || (result != 0)) {
		error("Could not find all the AD9081 channels\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	//Do an initial inspection of Channel Control regs to get the default state
	inspect_dac_regs();

	/****
	 * At this point the NCO and other parameters can be adjusted based on the
	 * application using the config channel loaded. For example:
	 * //Set NCO Freq
	 * if(iio_channel_attr_write_longlong(tx_channels[0].config_i, "main_nco_frequency", 800000000LL) < 0) {
	 * 		error("Could not set the NCO Freq\n");
	 *		ret = EXIT_FAILURE;
	 *		goto clean;
	 *	}
	 */


	/* Step 1: Set the DDS to Raw. This will actually set the TPL/DAC to Zero
	 * mode for all channels, and get it ready to have a scan mask
	 */
	info("Configuring for Raw Mode\n");
	if(iio_channel_attr_write_bool(dac1_dds_ctrl, "raw", false) < 0) {
		error("Could not set raw mode\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	//Do another inspection of Channel Control regs
	inspect_dac_regs();


	/* Step 2: Enable all the channels that want to be outputted. */
	info("Enabling Channels\n");
	for( i = 0; i < NUM_TX_CH; i++ ) {
		//Enabling the DAC channels
		iio_channel_enable(tx_channels[i].dac_i);
		iio_channel_enable(tx_channels[i].dac_q);
	}

	//Do another inspection of Channel Control regs
	inspect_dac_regs();


	/* Step 3: Create the sample buffer. In this case 65K-Samples * Num channels
	 * Not cyclic
	 */
	info("Opening the buffer\n");
	if((sample_buff = iio_device_create_buffer(ad9081_tx, 0x10000 * NUM_TX_CH * 2, false)) == NULL){
		error("Could not create data buffer\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	//Do another inspection of Channel Control regs
	inspect_dac_regs();

	info("Starting Writing\n");
	while( stop_loop == false ) {

		//Just do a simple linear ramp for testing purposes
		p_dat = iio_buffer_start(sample_buff);
		p_end = (uint16_t*)iio_buffer_end(sample_buff);
		while(p_dat < p_end) {
			for( i = 0; i < NUM_TX_CH; i++ ) {
				*p_dat++ = tx_channels[i].ramp_i = tx_channels[i].ramp_i + tx_channels[i].ramp_inc;
				*p_dat++ = tx_channels[i].ramp_q = tx_channels[i].ramp_q + tx_channels[i].ramp_inc;
			}
		}

		if((result = iio_buffer_push(sample_buff)) < 0) {
			error("Error code %d when pushing buffer\n", result);
			ret = EXIT_FAILURE;
			goto clean;
		}
	}
	info("Completed sampling\n");

clean:
	if(sample_buff) {
		info("Cleaning up the buffer\n");
		iio_buffer_destroy(sample_buff);

		//Small delay to let the driver cleanup
		usleep(500000);

		//Do another inspection of Channel Control regs
		inspect_dac_regs();
	}
	if(ctx) {
		iio_context_destroy(ctx);
	}
	return ret;
}
