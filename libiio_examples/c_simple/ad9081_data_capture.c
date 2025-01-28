/*
 * Simple example application dump AD9081 capture
 * data to a file.
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

#define ARGS(fmt, ...)	__VA_ARGS__
#define FMT(fmt, ...)	fmt
#define error(...) \
	printf("%s, %d: ERROR: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

#define info(...) \
	printf("%s, %d: INFO: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))


#define NUM_SAMPLE_LOOPS    20

static struct iio_context *ctx = NULL;

int main(int argc, char* argv[])
{
    int ret;
    int result;
    FILE* sample_file = NULL;
    int i;
    ssize_t refill_size;
    struct iio_device *ad9081 = NULL;
    struct iio_channel *adc0_i = NULL;
    struct iio_channel *adc0_q = NULL;
    struct iio_channel *adc1_i = NULL;
    struct iio_channel *adc1_q = NULL;
    struct iio_buffer  *sample_buff = NULL;

    if(argc < 2) {
        error("Not enough args. Expecting a filename\n");
        return EXIT_FAILURE;
    }

    if((sample_file = fopen(argv[1],"wb")) == NULL)
    {
        error("Couldn't create file %s\n", argv[1]);
        return EXIT_FAILURE;
    }

	ctx = iio_create_default_context();
	if (!ctx) {
		error("Could not create IIO context\n");
        ret = EXIT_FAILURE;
		goto clean;
	}

    //Devices are found by their IIO name
	ad9081 = iio_context_find_device(ctx, "axi-ad9081-rx-hpc");
	if (!ad9081) {
        error("Could not find AD9081 Device\n");
		ret = EXIT_FAILURE;
		goto clean;
	}

    //Channels are found by their channel name
    adc0_i = iio_device_find_channel(ad9081, "voltage0_i", false);
    adc0_q = iio_device_find_channel(ad9081, "voltage0_q", false);
    adc1_i = iio_device_find_channel(ad9081, "voltage1_i", false);
    adc1_q = iio_device_find_channel(ad9081, "voltage1_q", false);
    if (!adc0_i || !adc0_q || !adc1_i || !adc1_q) {
        error("Could not find all the AD9081 channels\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    //Set Ramp test mode. This is a channel attribute thats applied to all channels
    if(iio_channel_attr_write(adc0_i, "test_mode", "ramp") < 0) {
        error("Could not set the ramp test mode\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    //Enable the channels we want to sample
    iio_channel_enable(adc0_i);
    iio_channel_enable(adc0_q);
    iio_channel_enable(adc1_i);
    iio_channel_enable(adc1_q);

    //Create the sample buffer. 1M-Samples
    if((sample_buff = iio_device_create_buffer(ad9081, 1024*1024, false)) == NULL){
        error("Could not create data buffer\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    info("Starting Sampling\n");
    for(i = 0; i < NUM_SAMPLE_LOOPS; i++) {
        refill_size = iio_buffer_refill(sample_buff);
        if(refill_size < 0) {
            error("Error code %ld when refilling buffer\n", refill_size);
            ret = EXIT_FAILURE;
            goto clean;
        }
        fwrite(iio_buffer_start(sample_buff), 1, refill_size, sample_file);
    }
    info("Completed sampling\n");

clean:
    if(sample_file) {
        fclose(sample_file);
    }
    if(sample_buff) {
        iio_buffer_destroy(sample_buff);
    }
	if(ctx) {
        iio_context_destroy(ctx);
    }
	return ret;
}