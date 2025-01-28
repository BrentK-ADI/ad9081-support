# Patches
This folder contains a variety of patches for supporting modifications of the
AD9081 driver and components.

### 0001-AD9081-Explicit-PLL-Config-for-12Ghz-DAC-333MHz-Ref.patch
Due to integer math, the algorithm to calculate the PLL divisors to meet the DAC
and Ref clock constraints does not resolve to a valid solution for the 12GHz/333MHz
combinations due to the repeating decimal components of 333MHz (333333333.3333~Hz)
needed for the math to work out.

This modification adds an explicit case for 12GHz/333MHz to set N=8, M=9, R=2
and bypass the auto calculation algorithm.

Baseline: [54eb23f](https://github.com/analogdevicesinc/linux/commits/54eb23f4b5c6093916f208772627f7b68f495559)
