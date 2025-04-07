# Patches
This folder contains a variety of patches for supporting modifications of the
AD9081 driver and components.

### 0001-AD9081-Add-Explicit-PLL-LUT.patch
Due to integer math, the algorithm to calculate the PLL divisors to meet the DAC
and Ref clock constraints does not resolve to a valid solution for a variety of
combinations due to the repeating decimal components of some reference clock
frequencies.

This modification adds a lookup table to define explicit cases for the DAC and
REF clocks, along with the PLL parameters necessary for configuration. By
leveraging this table, additional explicit configurations should be easily
added.

Baseline: [54eb23f](https://github.com/analogdevicesinc/linux/commits/54eb23f4b5c6093916f208772627f7b68f495559)

### 0002-AD9081-Add-direct-DMA-enable-attribute.patch
This patch provides an IIO attribute for enabling DMA data flow in the TX IP core
directly, without needing to open an IIO Buffer. This is a useful feature when
a Tx data stream is generated in HDL and can be switched in place of the DMA
stream from the IIO buffering system.

The patch uses the PROCESSED IIO attribute, which will expose itself as the
`out_voltage_input` file for the Tx IIO device.   If set to a 1/True, the IP
core will set all channels to DMA mode and set the equivalent registers as if
a buffer was created. If transitioned to a 0/False, the channels will be set
back to DDS mode.

This new mode is mutually exclusive to buffer mode.  If a buffer is opened,
setting `out_voltage_input` will return a -EBUSY error. Similarly, if this mode
is enabled, opening a buffer or setting `raw` on one of the DDS channels will
also result in a -EBUSY error.

See [ad9081_processed_test.c](../libiio_examples/processed_patch_test/ad9081_processed_test.c)
for a sample libiio based application which tests the mutual exclusivity of the
updates.

Baseline: [54eb23f](https://github.com/analogdevicesinc/linux/commits/54eb23f4b5c6093916f208772627f7b68f495559)

### **(OBSOLETE)** 0001-AD9081-Explicit-PLL-Config-for-12Ghz-DAC-333MHz-Ref.patch **(OBSOLETE)**
Due to integer math, the algorithm to calculate the PLL divisors to meet the DAC
and Ref clock constraints does not resolve to a valid solution for the 12GHz/333MHz
combinations due to the repeating decimal components of 333MHz (333333333.3333~Hz)
needed for the math to work out.

This modification adds an explicit case for 12GHz/333MHz to set N=8, M=9, R=2
and bypass the auto calculation algorithm.

Baseline: [54eb23f](https://github.com/analogdevicesinc/linux/commits/54eb23f4b5c6093916f208772627f7b68f495559)

Note: This is superseded by the AD9081-Add-Explicit-PLL_LUT file which uses a
table to store as many explicit configurations as required. This 12GHz/333MHz
version is maintained here for historical reasons.

