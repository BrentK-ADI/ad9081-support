/*
 * Example application testing the driver patch which leverages the Processed
 * attribute ('input') for the AD9081 Tx channels to forcefully enable DMA mode
 * for all channels without needing to open a buffer.
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

/* Helper struct for holding channel pair information. The dds_ channels are
 * from the ad9081 DDS engine.
 * The dac_ channels are from the DAC/DMA block and are use for setting up the
 * data buffers
 */
typedef struct {
	struct iio_channel *dds1_i;
	struct iio_channel *dds1_q;
	struct iio_channel *dds2_i;
	struct iio_channel *dds2_q;
	struct iio_channel *dac_i;
	struct iio_channel *dac_q;
} tx_channel_pair_t;

/* Holds all the channel pairs */
static tx_channel_pair_t tx_channels[NUM_TX_CH];

static struct iio_context *ctx = NULL;
static struct iio_device *ad9081 = NULL;
static struct iio_device *ad9081_tx = NULL;

/**
 * Helper function to inspect register values from the DAC engine for
 * each channel
 */
static uint32_t get_dac_reg(int ch, int reg)
{
	int i;
	uint32_t reg_val;
	uint32_t reg_addr;
	reg_addr = DAC_CH_REG_BASE + (ch * DAC_CH_REG_STEP) + reg;
	iio_device_reg_read(ad9081_tx, reg_addr, &reg_val);
	return reg_val;
}

/**
 * Loads the Tx channels into the tx_channels array.  Loads both the DDS and DMA
 * channels from the Tx engine.
 */
static int load_channels(void)
{
	int i;
	int result = 0; //Assume successful
	char ch_buff[TEMP_BUFF_LEN];

	for(i = 0; i < NUM_TX_CH; i++) {
		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_i", i);
		if((tx_channels[i].dac_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output (DAC) Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "voltage%d_q", i);
		if((tx_channels[i].dac_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Output (DAC) Channel %s\n",ch_buff);
		}

		/* Tone indexes start at 1, not 0.  The DDS engines support independent
		 * I & Q signal generation on each output channel, for 2 tones, for a
		 * total of 4 IIO channels each.
		 */
		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_I_F1", i+1);
		if((tx_channels[i].dds1_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 1 I Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_Q_F1", i+1);
		if((tx_channels[i].dds1_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 1 Q Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_I_F2", i+1);
		if((tx_channels[i].dds2_i = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 2 I Channel %s\n",ch_buff);
		}

		snprintf(ch_buff, TEMP_BUFF_LEN, "TX%d_Q_F2", i+1);
		if((tx_channels[i].dds2_q = iio_device_find_channel(ad9081_tx, ch_buff, true)) == NULL) {
			result = -1;
			error("Error finding Tone 2 Q Channel %s\n",ch_buff);
		}
	}

	return result;
}


/* Helper macro to check conditions, print some error and jump to the end */
#define TEST_ASSERT(cond) \
	if(!(cond)) { error("Test failure!\n"); goto clean; }

int main(int argc, char* argv[])
{
	int ret;
	int result;
	int i;
	bool bval;
	uint32_t uval;
	struct iio_buffer  *sample_buff = NULL;

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
	if (result != 0) {
		error("Could not find all the AD9081 channels\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

	/**************************************************
	 * Verify the test is in a good place to start
	 **************************************************/
	printf("Verifying Processed/Input is disabled to start...\n");
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);


	/**************************************************
	 * Setup a normal DMA operation from memory. Raw Mode = 0, creating
	 * a buffer, etc.  The DAC channels should goto Zero (0x3), when raw
	 * mode is kicked in, then to 0x2 (DMA) when the buffer is opened. Once the
	 * buffer is opened, Input should be locked out
	 **************************************************/
	printf("Setting Raw = 0. Verifying Registers...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", false);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x3);
	}

	printf("Creating a DMA buffer...\n");
	for( i = 0; i < NUM_TX_CH; i++) {
		iio_channel_enable(tx_channels[i].dac_i);
		iio_channel_enable(tx_channels[i].dac_q);
	}
	sample_buff = iio_device_create_buffer(ad9081_tx, 0x10000 * NUM_TX_CH * 2, false);
	TEST_ASSERT(sample_buff != NULL);

	printf("Verifying Processed/Input Locked Out...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", true);
	TEST_ASSERT(result == -EBUSY);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);

	printf("Verifying Registers are DMA Mode...\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x2);
	}

	printf("Destroying The Buffer...\n");
	iio_buffer_destroy(sample_buff);
	usleep(500000);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x0);
	}

	/**************************************************
	 * Setup our new processed/input mode.  Raw is set to 0 to start this, which
	 * means the DAC regs will be Zero (0x3). Once input is enabled, the DAC
	 * registers should be DMA (0x2).  Buffering and Raw is locked out while
	 * Input is enabled. Once input is disabled, all the channels back to DDS.
	 **************************************************/
	printf("Setting Raw = 0. Verifying Registers...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", false);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x3);
	}

	printf("Enabling Processed/Input Mode...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", true);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == true);

	printf("Verifying Registers are DMA...\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x2);
	}

	printf("Verifying RAW is locked out...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", false);
	TEST_ASSERT(result == -EBUSY);
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", true);
	TEST_ASSERT(result == -EBUSY);

	printf("Verifying Buffers locked out...\n");
	sample_buff = iio_device_create_buffer(ad9081_tx, 0x10000 * NUM_TX_CH * 2, false);
	TEST_ASSERT(sample_buff == NULL);

	printf("Disabling Processed/Input mode...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", false);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);

	printf("Verifying Registers back to DDS...\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x0);
	}


	/**************************************************
	 * Setup a normal DMA operation from memory again to make sure the
	 * functionality comes back.
	 **************************************************/
	printf("Enabling Buffers again...\n");
	for( i = 0; i < NUM_TX_CH; i++) {
		iio_channel_enable(tx_channels[i].dac_i);
		iio_channel_enable(tx_channels[i].dac_q);
	}
	sample_buff = iio_device_create_buffer(ad9081_tx, 0x10000 * NUM_TX_CH * 2, false);
	TEST_ASSERT(sample_buff != NULL);

	printf("Verifying Processed/Input is locked out...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", true);
	TEST_ASSERT(result == -EBUSY);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);

	printf("Verifying Registers are DMA...\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x2);
	}

	printf("Destroying the Buffer...\n");
	iio_buffer_destroy(sample_buff);
	usleep(500000);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x0);
	}


	/**************************************************
	 * Setup our new processed/input mode. This time Raw is set to 1 to start,
	 * which means the DAC regs will be DDS (0x0). Once input is enabled, the DAC
	 * registers should be DMA (0x2).  Buffering and Raw is locked out while
	 * Input is enabled. Once input is disabled, all the channels back to DDS.
	 **************************************************/
	printf("Setting Raw = 1. Verifying Registers...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", true);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x0);
	}

	printf("Enabling Processed/Input Mode...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", true);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == true);

	printf("Verifying Registers are DMA...\n");
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x2);
	}

	printf("Verifying RAW locked out...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", false);
	TEST_ASSERT(result == -EBUSY);
	result = iio_channel_attr_write_bool(tx_channels[0].dds1_i, "raw", true);
	TEST_ASSERT(result == -EBUSY);


	printf("Verifying Buffers locked out...\n");
	sample_buff = iio_device_create_buffer(ad9081_tx, 0x10000 * NUM_TX_CH * 2, false);
	TEST_ASSERT(sample_buff == NULL);

	printf("Disabling Processed/Input Mode...\n");
	result = iio_channel_attr_write_bool(tx_channels[0].dac_i, "input", false);
	TEST_ASSERT(result == 0);
	result = iio_channel_attr_read_bool(tx_channels[0].dac_i, "input", &bval);
	TEST_ASSERT(result == 0);
	TEST_ASSERT(bval == false);
	for( i = 0; i < NUM_TX_CH * 2; i++ ) {
		uval = get_dac_reg(i, DAC_CH_CTRL_OFFSET);
		TEST_ASSERT(uval == 0x0);
	}

	printf("Test Completed Successfully!\n");

clean:
	if(ctx) {
		iio_context_destroy(ctx);
	}
	return ret;
}
