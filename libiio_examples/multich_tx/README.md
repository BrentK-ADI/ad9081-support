# libiio Examples - multich_tx

The following example shows setting up multiple (in this case all) the channels
for Tx and the impact the stages of configuration have on the underlying DAC
IP block's registers.

At each step of the way, the REG_CHAN_CNTRL_7 register of the DAC channels is
monitored to determine state.

## Building
To build this application, simply run GCC while linking against libiio and
libmath:

`gcc ad9081_multich_tx.c -liio -o ad9081_multich_tx`

*NOTE:*This application attempts to configure 8 pairs of I&Q for each Rx and Tx
data path.  The default HDL and device tree configuration in Kuiper Linux (m8_l4)
only has 4 pairs in each direction.  The NUM_CH constant can be redefined at
build time to only support the first 4 channels:

`gcc ad9081_multich_tx.c -liio -DNUM_TX_CH=4 -o ad9081_multich_tx_4ch`

## Expected Output
The following shows an example output when running with 4 channels, all enabled:

```
$ IIOD_REMOTE=ip:192.168.1.155 ./ad9081_multich_tx

main, 171: INFO: Loading Channels
**DAC Regs**
Ch 0: CTRL7 (0x418) = 0x00
Ch 1: CTRL7 (0x458) = 0x00
Ch 2: CTRL7 (0x498) = 0x00
Ch 3: CTRL7 (0x4D8) = 0x00
Ch 4: CTRL7 (0x518) = 0x00
Ch 5: CTRL7 (0x558) = 0x00
Ch 6: CTRL7 (0x598) = 0x00
Ch 7: CTRL7 (0x5D8) = 0x00


main, 201: INFO: Configuring for Raw Mode
**DAC Regs**
Ch 0: CTRL7 (0x418) = 0x03
Ch 1: CTRL7 (0x458) = 0x03
Ch 2: CTRL7 (0x498) = 0x03
Ch 3: CTRL7 (0x4D8) = 0x03
Ch 4: CTRL7 (0x518) = 0x03
Ch 5: CTRL7 (0x558) = 0x03
Ch 6: CTRL7 (0x598) = 0x03
Ch 7: CTRL7 (0x5D8) = 0x03


main, 213: INFO: Enabling Channels
**DAC Regs**
Ch 0: CTRL7 (0x418) = 0x03
Ch 1: CTRL7 (0x458) = 0x03
Ch 2: CTRL7 (0x498) = 0x03
Ch 3: CTRL7 (0x4D8) = 0x03
Ch 4: CTRL7 (0x518) = 0x03
Ch 5: CTRL7 (0x558) = 0x03
Ch 6: CTRL7 (0x598) = 0x03
Ch 7: CTRL7 (0x5D8) = 0x03


main, 227: INFO: Opening the buffer
**DAC Regs**
Ch 0: CTRL7 (0x418) = 0x02
Ch 1: CTRL7 (0x458) = 0x02
Ch 2: CTRL7 (0x498) = 0x02
Ch 3: CTRL7 (0x4D8) = 0x02
Ch 4: CTRL7 (0x518) = 0x02
Ch 5: CTRL7 (0x558) = 0x02
Ch 6: CTRL7 (0x598) = 0x02
Ch 7: CTRL7 (0x5D8) = 0x02


main, 237: INFO: Starting Writing
^Cmain, 256: INFO: Completed sampling
main, 260: INFO: Cleaning up the buffer
**DAC Regs**
Ch 0: CTRL7 (0x418) = 0x00
Ch 1: CTRL7 (0x458) = 0x00
Ch 2: CTRL7 (0x498) = 0x00
Ch 3: CTRL7 (0x4D8) = 0x00
Ch 4: CTRL7 (0x518) = 0x00
Ch 5: CTRL7 (0x558) = 0x00
Ch 6: CTRL7 (0x598) = 0x00
Ch 7: CTRL7 (0x5D8) = 0x00
```