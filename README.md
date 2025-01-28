# Overview

This repository contains a variety of support and reference files for working
with the Analog Devices' [AD9081](https://www.analog.com/en/products/ad9081.html)
MxFE Quad, 16-bit, 12GSPS RFDAC and Quad, 12-bit, 4GSPS RFADC.

Most of the content here was created using the [AD9081-FMCA-EBZ](https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/eval-ad9081.html) FMC Evaluation card on a Carrier such as the Xilinx
[ZCU102](https://www.xilinx.com/products/boards-and-kits/ek-u1-zcu102-g.html) or
[VCK190](https://www.xilinx.com/products/boards-and-kits/vck190.html) Evaluation platforms.

The code can be supported by ADI's [Kuiper Linux](https://wiki.analog.com/resources/tools-software/linux-software/kuiper-linux),
Petalinux with [meta-adi](https://github.com/analogdevicesinc/meta-adi) or
any other Linux distribution you wish.

## Patches
This folder contains various driver and other patches to support use cases for
the AD9081.  Often times, these patches solve a unique fringe use case of the
device, not immediately covered by the baseline driver package.

## libiio Examples
This folder contains example projects using [libiio](https://github.com/analogdevicesinc/libiio)
to interface with the device from Linux userspace.  These examples go beyond
the [basic examples](https://github.com/analogdevicesinc/libiio/tree/main/examples)
found in the libiio source tree, often performing more advanced functionality specific
to the AD9081 or its unique use case.
