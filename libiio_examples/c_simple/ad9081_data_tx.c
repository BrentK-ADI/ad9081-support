/*
 * Simple example application send data out AD9081
 *
 * Copyright © 2024 by Analog Devices, Inc.  All rights reserved.
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
#include <math.h>
#include <unistd.h>

#define ARGS(fmt, ...)	__VA_ARGS__
#define FMT(fmt, ...)	fmt
#define error(...) \
	printf("%s, %d: ERROR: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))

#define info(...) \
	printf("%s, %d: INFO: " FMT(__VA_ARGS__, 0)"%s", __func__, __LINE__, ARGS(__VA_ARGS__, ""))


#define OUTPUT_AMP  16000

static const double F_STEPS[] = { (M_PI / 100.0), (M_PI / 500.0)};
static const int num_fsteps = sizeof(F_STEPS)/sizeof(double);
static bool stop_loop = false;

static struct iio_context *ctx = NULL;

static void handle_sig(int sig)
{
    stop_loop = true;
}

int main(int argc, char* argv[])
{
    int ret;
    int result;
    int f_idx = 0;
    int i;
    double sin_val = 0.0;
    int16_t* p_dat, *p_end;
    ptrdiff_t p_inc;

    struct iio_device *ad9081 = NULL;
    struct iio_device *ad9081_tx = NULL;
    struct iio_channel *dac1_i = NULL;
    struct iio_channel *dac1_q = NULL;
    struct iio_channel *dac1_i_config = NULL;
    struct iio_channel *dac1_q_config = NULL;
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

    //Channels are found by their channel name
    dac1_i_config = iio_device_find_channel(ad9081, "voltage1_i", true);
    dac1_q_config = iio_device_find_channel(ad9081, "voltage1_q", true);
    dac1_i = iio_device_find_channel(ad9081_tx, "voltage1_i", true);
    dac1_q = iio_device_find_channel(ad9081_tx, "voltage1_q", true);
    dac1_dds_ctrl = iio_device_find_channel(ad9081_tx, "altvoltage0", true);
    if (!dac1_i || !dac1_q || !dac1_i_config || !dac1_q_config || !dac1_dds_ctrl) {
        error("Could not find all the AD9081 channels\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    //Set NCO Freq
    if(iio_channel_attr_write_longlong(dac1_i_config, "main_nco_frequency", 800000000LL) < 0) {
        error("Could not set the NCO Freq\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    if(iio_channel_attr_write_bool(dac1_dds_ctrl, "raw", false) < 0) {
        error("Could not set raw mode\n");
        ret = EXIT_FAILURE;
        goto clean;
    }

    //Enable the channels we want to sample
    iio_channel_enable(dac1_i);
    iio_channel_enable(dac1_q);

    //Create the sample buffer. 1M-Samples
    if((sample_buff = iio_device_create_buffer(ad9081_tx, 1024*1024, true)) == NULL){
        error("Could not create data buffer\n");
		ret = EXIT_FAILURE;
		goto clean;
    }

    info("Starting Writing\n");
    p_inc = iio_buffer_step(sample_buff);
    p_end = iio_buffer_end(sample_buff);
    for( p_dat = iio_buffer_first(sample_buff, dac1_i);
         p_dat < p_end;
         p_dat += p_inc/sizeof(*p_dat))
    {
        sin_val += F_STEPS[f_idx];

        p_dat[0] = (int16_t)(cos(sin_val) * OUTPUT_AMP);
        p_dat[1] = (int16_t)(sin(sin_val) * OUTPUT_AMP);

       if(sin_val >=  (2.0*M_PI))
        {
            sin_val -= (2.0*M_PI);
            f_idx = (f_idx + 1) % num_fsteps;
        }
    }

    if((result = iio_buffer_push(sample_buff)) < 0) {
        error("Error code %d when pushing buffer\n", result);
        ret = EXIT_FAILURE;
        goto clean;
    }

    while(stop_loop == false) {
        usleep(500000);
    }
    info("Completed sampling\n");

clean:
    if(sample_buff) {
        iio_buffer_destroy(sample_buff);
    }
	if(ctx) {
        iio_context_destroy(ctx);
    }
	return ret;
}
