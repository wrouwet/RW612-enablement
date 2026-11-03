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
make build APP=wifi     # APP is ble_shell (default), blinky, wifi_ble, wireless_uart, wifi, ble or a path to an application
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
| `wireless_uart` | `apps/wireless_uart` | Bridges a UART to BLE for the Wireless UART screen of NXP's IoT Toolbox app |
| `ble_shell` | `apps/ble_shell` | Zephyr console on a phone over BLE, with Wi-Fi commands and logs |
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

## Wireless UART

`apps/wireless_uart` implements NXP's Wireless UART profile, so the IoT Toolbox phone
app can exchange data with a UART on the board. The bridge uses the USB serial port on
J10 by default, and it also prints status messages there, such as `Connected`.

```bash
make flash APP=wireless_uart
make monitor
```

In IoT Toolbox, open Wireless UART and connect to `NXP_WU`.

To bridge a different UART, change `app,bridge-uart` in
`apps/wireless_uart/boards/frdm_rw612.overlay`.

Limitations:

- **No flow control:** there are 4 KB buffers in each direction. Data beyond that is
  lost, which can happen when the phone sends long bursts faster than the UART's
  115200 baud.
- **No pairing:** any nearby BLE device can connect and read or write the UART, so
  don't bridge anything sensitive.
- **One connection:** only one phone can connect at a time.

## Zephyr console on a phone

`apps/ble_shell` is the default application and the board's main console. It runs the
Zephyr shell and log output over the same Wireless UART profile, with Wi-Fi and BLE in
one image. It greets you when you connect, reports the Wi-Fi status every 30 seconds,
and reports changes as they happen.

```bash
make flash APP=ble_shell
```

In IoT Toolbox, open Wireless UART, connect to `NXP_WU`, and type commands such as
`help`, `wifi scan` or `net iface`. To join a network:

```
wifi connect -s <SSID> -k 1 -p <password>
```

The board allows one BLE connection at a time, so close the connection on any other
device first, or they will compete for it and the link will keep dropping.

Use `-k 3` for WPA3 and `-k 0` for an open network. Logs also go to J10, which is the
only place to see those from before the phone connects.

**Security:** this app has no pairing or encryption, on purpose, for bench use. Any
nearby BLE device can connect and run shell commands, and Wi-Fi passwords typed on the
phone cross the air unencrypted. Add pairing before using it outside a test setup.

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
