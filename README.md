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
make wifi-test          # flash wifi_ble, join a network, then test ping and TCP
make menuconfig APP=wifi
make build-all
make clean
```

| APP | Source | What it does |
|---|---|---|
| `blinky` | `apps/blinky` | Blinks the green LED and logs each change to the console |
| `wifi_ble` | `apps/wifi_ble` | Wi-Fi shell and BLE together. A BLE characteristic reports the Wi-Fi status |
| `wifi` | Zephyr `samples/net/wifi/shell` | Wi-Fi shell |
| `ble` | Zephyr `samples/bluetooth/peripheral` | BLE peripheral |

Build output goes to `build/<app>/`.

## Testing Wi-Fi

`make wifi-test` prompts for the network name, security type and password, then
checks association, DHCP, ping and TCP. The password stays out of the log, which is
written to `build/wifi-test.log`. The board stays connected afterwards.

The `wifi_ble` app advertises as `RW612 WiFi+BLE`. Its status characteristic
(`52573631-3200-4000-8000-525736313201`) can be read and subscribed to, and reports
`disconnected`, `associated`, `connected <ip>` or `connect failed`. Pairing isn't
required, so any nearby BLE device can read the status, including the IP address.

## Flashing

The FRDM-RW612's MCU-Link debug probe ships with SEGGER J-Link firmware, so flashing
needs the J-Link software. `make setup` doesn't install it because you have to accept
SEGGER's license yourself:

1. Download the Linux x86_64 `.tgz` from <https://www.segger.com/downloads/jlink/>.
2. Extract it into the workspace (tested with V9.90):

   ```bash
   mkdir -p ../.tools/JLink
   tar -xzf JLink_Linux_x86_64.tgz -C ../.tools/JLink --strip-components=1
   ```

3. Connect USB to J10, then run `make flash`.

## Notes

- **Radio firmware:** `make blobs` downloads NXP's RW61x firmware, which is covered
  by the NXP software license in
  `modules/hal/nxp/zephyr/blobs/license/LA_OPT_NXP_Software_License.txt`. The blobs
  are not stored in this repo. The build links them into the application image.
- **Wi-Fi sample security:** Zephyr's Wi-Fi shell sample sets
  `CONFIG_TEST_RANDOM_GENERATOR=y`, which is not secure. Don't use its configuration
  in production.
