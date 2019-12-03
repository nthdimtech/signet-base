## Internal Flash Memory layout

The original Signet divided it's flash memory into a code region and a storage
region; the storage contained identification data, authentication data, and the
encrypted personal information database. Signet HC is not well-suited for this
scheme because it contains large flash memory sectors of unequal size (ranging
from 16K to 128K) which make storage management more difficult size because:

- Adding or changing parts of a large sector would take a long time and increase
  the risk of data loss if device power were interrupted during a write

- The amount of code needed to implement Signet HC's features is greater which
  would make it difficult to make the database large enough to handle all of
  device's use cases adaquitely.

Instead of storing the database in the MCU flash memory, it will be stored in
the first 16MB of it's eMMC memory, still leaving pleanty of space for mass
storage volumes. The device identification and authentication data will still be
stored inside the MCU for added security. Two versions of this data is stored in
the first two flash sectors in the MCU, which are 16KB echo. We refer to these
sectors as root sectors. Only one root sector contains data that will be used by
the device. The other is used for for redundancy. The device uses counters
contained in the root sectors to determine which sector to use. We refer to
these counters as modification counters. We refer to the two root sectors as
root sector A and and root sector B. See root sector management for more
details.

After the root sectors is two separate firmware programs. The bootloader program
and the application program. The bootloader program contains a portion of the
Signet HC functionality but only enough to update the Signet firmware. The
application firmware program contains all Signet HC features. In order to
perform a firmware updgrade, the application firmware switches the active program
to the bootloader and restarts the device. The bootloader will perform the
upgrade and switch back to the application program when done. The bootloader
program is 96KB in size and runs completely in RAM so that it can update itself
in flash memory. The application program is 384KB. See firmware updgrade
procedure for more details.

## Root sector management

Every time a change needs to be made to the active root sector the following
procedure is followed:

- The firmware copies the active root sector is made into RAM

- The firmware makes the desired modification is made and increments the
  sector's modification counter

- If the active root sector is A the firwmare writes the modified copy of the
  sector in RAM to root sector B. Similarly if the active root sector is B then
  the firmware writes the modified copy of the sector in RAM to sector A. This
  ensures that there is always a valid root sector if the device power is
  interrupted while the root sector is being updated

- After the write is completed the updated sector is also written to its
  original position so that the process completes with two copies of the most
  recent root sector data in the MCU flash.

To allow the device to correctly detect when a root sector has not been written
to correctly or completely we include a CRC value in the root sector data
structure so the firmware can test each sectors's data integrety. The device
chooses an active root sector that has the largest modification counter of the
sectors with a valid CRC value. If both sectors are identical then sector A is
chosen. If the startup process detects an invalid or out date sector then it
will copy the active (i.e. valid) sector to the invalid one to achieve
redundancy.

## Firmware upgrade procedure

The firmware upgrade procedure may begin from either the bootloader program
or the application program. The process begins either by the user selecting
a firmware update file. The firmware update file contains new versions of
the bootloader program and the application program and a header with CRC
values and signatures for validation. Once selected the GUI uploads the
file to the device which begins the firmware update process. A field in the
root sector tracks the state of the upgrade process so the process can be
resumed if it is interrupted. The process states are:

- Firmware valid
- Firmware file downloaded
- Bootloader only upgraded
- Application only upgraded

When the GUI has uploaded a firmware update file to the device the firmware
begins the upgrade process by executing the following steps:

- Write the firmware update file verbatim to a 512KB region of eMMC flash
reserved for the firmware update process

- Perform CRC and signature validation of bootloader and application firmware.
  Cancel the upgrade if any validiation check fails.

- Set state to "firmware file downloaded"

The next steps depend on whether the application firmware is running or whether
the bootloader firmware is starting the upgrade process.

### Application firmware: "Firmware file downloaded" state

- Erase the bootloader firmware and replace it with the the new bootloader
  firmware stored eMMC memory.

- Perform bootloader firmware CRC and signature checks directly on the
  bootloader flash memory in the MCU.

- If validation tests pass then set the upgrade state to "Bootloader only
  upgraded"

- Set the bootloader firmware to the active program

- Restart the MCU

### Bootloader firmware: "Firmware file downloaded" state

- Erase the application firmware and replace it with the the new application
  firmware stored eMMC memory.

- Perform application firmware CRC and signature checks directly on the
  application firmware flash memory in the MCU.

- If validation tests pass then set the upgrade state to "Application only
  upgraded"

- Set the application firmware as the active program

- Erase the bootloader firmware and replace it with the the new bootloader
  firmware stored eMMC memory.

- Perform bootloader firmware CRC and signature checks directly on the
  bootloader firmware flash memory in the MCU.

- Set the upgrade state to "Firmware valid"

- Restart the MCU

### Application firmware: "Application only upgraded" state

- Erase the bootloader firmware and replace it with the the new bootloader
  firmware stored eMMC memory.

- Perform bootloader firmware CRC and signature checks directly on the
  bootloader firmware flash memory in the MCU.

- Set the upgrade state to "Firmware valid"

- Restart the MCU

### Bootloader firmware: "Bootloader only upgraded" state

- Erase the application firmware and replace it with the the new application
  firmware stored eMMC memory.

- Perform application firmware CRC and signature checks directly on the
  application firmware flash memory in the MCU.

- Set the upgrade state to "Firmware valid"

- Set the application firmware as the active program

- Restart the MCU
