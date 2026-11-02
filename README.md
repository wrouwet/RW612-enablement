# RW612 enablement

Makefile-based [Zephyr](https://zephyrproject.org/) development environment for the
NXP FRDM-RW612 board (Zephyr board name `frdm_rw612`), with Wi-Fi and BLE.

| Component | Version |
|---|---|
| Zephyr | v4.5.0 (pinned in `west.yml`) |
| Zephyr SDK | 1.0.1, `arm-zephyr-eabi` toolchain only |
| Python | 3.12, installed locally by `uv` |

## Setup

Tested on Ubuntu 26.04 x86_64. Clone into an empty directory, because the Zephyr
workspace (about 4.5 GB) is created next to this repo:

```bash
mkdir rw612-workspace && cd rw612-workspace
git clone https://github.com/wrouwet/RW612-enablement.git
cd RW612-enablement
sudo scripts/install-host-deps.sh
make setup
make blobs
```

Only `install-host-deps.sh` needs sudo. Everything else stays inside the workspace.

## Usage

```bash
make build APP=wifi     # APP is blinky (default), wifi, ble or a path to an application
make flash APP=wifi     # add RUNNER=jlink or RUNNER=linkserver to choose the flash tool
make monitor            # serial console at 115200 baud, PORT=/dev/ttyACM0 by default
make menuconfig APP=wifi
make build-all
make clean
```

| APP | Source | What it does |
|---|---|---|
| `blinky` | `apps/blinky` | Blinks the green LED and logs each change to the console |
| `wifi` | Zephyr `samples/net/wifi/shell` | Wi-Fi shell |
| `ble` | Zephyr `samples/bluetooth/peripheral` | BLE peripheral |

Build output goes to `build/<app>/`.

## Flashing

Flashing needs a host tool that matches the firmware on the board's MCU-Link debug
probe: SEGGER J-Link or NXP LinkServer. Neither is installed by `make setup`, as
each has its own license terms.

## Notes

- **Radio firmware:** `make blobs` downloads NXP's RW61x firmware, which is covered
  by the NXP software license in
  `modules/hal/nxp/zephyr/blobs/license/LA_OPT_NXP_Software_License.txt`. The blobs
  are not stored in this repo. The build links them into the application image.
- **Wi-Fi sample security:** Zephyr's Wi-Fi shell sample sets
  `CONFIG_TEST_RANDOM_GENERATOR=y`, which is not secure. Don't use its configuration
  in production.
