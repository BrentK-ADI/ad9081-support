/*
 * Simple example application for working with attributes
 * on a HMC7044 device
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


static struct iio_context *ctx = NULL;

int main(void)
{
    int ret;
    int result;
    char dummy_buff[256];
    double dummy_double;
    long long dummy_int;

    struct iio_device *hmc7044 = NULL;
    struct iio_channel *refclk2 = NULL;
    struct iio_channel *coretx = NULL;

	ctx = iio_create_default_context();
	if (!ctx) {
		error("Could not create IIO context\n");
		return EXIT_FAILURE;
	}

    //Devices are found by their IIO name
	hmc7044 = iio_context_find_device(ctx, "hmc7044");
	if (!hmc7044) {
        error("Could not find HMC7044 Device\n");
		return EXIT_FAILURE;
		goto clean;
	}

    //Channels are found by their channel name
    coretx = iio_device_find_channel(hmc7044, "altvoltage6", true);
    if (!coretx) {
        error("Could not find HMC7044 CORE_CLK_TX Channel\n");
		return EXIT_FAILURE;
		goto clean;
    }

    //Or, Extended-Name if supported by that driver
    refclk2 = iio_device_find_channel(hmc7044, "FPGA_REFCLK2", true);
    if (!refclk2) {
        error("Could not find HMC7044 FPGA_REFCLK2 Channel\n");
		return EXIT_FAILURE;
		goto clean;
    }

    /** The following code should have error checks on the function calls.
     *  Omitted for consolidated example code purposes.
     */
    //Read an attribute as just text, double or int
    iio_channel_attr_read(coretx, "frequency", dummy_buff, 256);
    iio_channel_attr_read_double(coretx, "frequency", &dummy_double);
    iio_channel_attr_read_longlong(coretx, "frequency", &dummy_int);
    info("CORE_TX Freq: %s | %f | %lld\n\n", dummy_buff, dummy_double, dummy_int);

    //Try changing the Frequency as a double
    info("Setting %f via double..\n", dummy_double / 2.0);
    iio_channel_attr_write_double(coretx, "frequency", dummy_double / 2.0);
    iio_channel_attr_read_double(coretx, "frequency", &dummy_double);
    info("CORE_TX Freq: %f\n\n", dummy_double);

    //Try changing the Frequency as an int
    info("Setting %lld via int..\n", dummy_int / 4);
    iio_channel_attr_write_longlong(coretx, "frequency", dummy_int / 4);
    iio_channel_attr_read_longlong(coretx, "frequency", &dummy_int);
    info("CORE_TX Freq: %lld\n\n", dummy_int);

    //Try changing the frequency as a string
    info("Setting %s via string/raw..\n", dummy_buff);
    iio_channel_attr_write(coretx, "frequency", dummy_buff);
    iio_channel_attr_read_longlong(coretx, "frequency", &dummy_int);
    info("CORE_TX Freq: %lld\n\n", dummy_int);

    //Show what happens when an invalid attribute is referenced
    result = iio_channel_attr_read(refclk2, "freq", dummy_buff, 256);
    info("Invalid attribute names return: %d\n\n", result);

    //Show what happens when an invalid value is provided
    result = iio_device_attr_write(hmc7044, "sync_pin_mode", "invalid_value");
    info("Invalid values return: %d\n\n", result);

clean:
	iio_context_destroy(ctx);
	return ret;
}