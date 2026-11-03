# RW612 enablement

Makefile-based [Zephyr](https://zephyrproject.org/) development environment for the
NXP FRDM-RW612 board (Zephyr board name `frdm_rw612`), with Wi-Fi and BLE.

The main way to use the board is a **Zephyr console over BLE**. From a serial terminal
on another computer, through a USB-to-BLE UART bridge, you can run shell commands, join
Wi-Fi networks and watch the logs and Wi-Fi status, with no cable to the RW612's console.

| Component | Version |
|---|---|
| Zephyr | v4.5.0, pinned in `west.yml` |
| Zephyr SDK | 1.0.1, `arm-zephyr-eabi` toolchain only |
| Python | 3.12, installed locally by `uv` |
| SEGGER J-Link | Tested with V9.90, installed manually |

This is a bench project, tested on one board. The console has no pairing or encryption, so read the [security note](docs/ble-console.md#security) before using it anywhere but a
test setup.

## Quick start

Tested on Ubuntu 26.04 x86_64. Clone into an empty directory, because the Zephyr
workspace is created next to this repo:

```bash
mkdir rw612-workspace && cd rw612-workspace
git clone https://github.com/wrouwet/RW612-enablement.git
cd RW612-enablement
sudo scripts/install-host-deps.sh
make setup
make blobs
```

Only `install-host-deps.sh` needs sudo. Everything else installs inside the workspace,
which takes about 4.5 GB before you build anything. Builds also keep a small cache in
`~/.cache/zephyr`.

To flash the board, you also need the J-Link software, which `make setup` doesn't
install because you have to accept SEGGER's license yourself:

1. Download the Linux x86_64 `.tgz` from <https://www.segger.com/downloads/jlink/>.
2. Extract it into the workspace:

   ```bash
   mkdir -p ../.tools/JLink
   tar -xzf JLink_Linux_x86_64.tgz -C ../.tools/JLink --strip-components=1
   ```

Then connect a USB cable to J10 and flash the default app:

```bash
make flash
```

Then connect to `NXP_WU` from a Wireless UART client. See [BLE console](docs/ble-console.md) for
which clients work and what to do next.

## Commands

`APP` selects the application, and defaults to `ble_shell`.

| Command | What it does |
|---|---|
| `make build APP=<app>` | Build an application |
| `make flash APP=<app>` | Build and flash it. `RUNNER=jlink` selects the flash tool explicitly |
| `make monitor` | Open the J10 serial port at 115200 baud. Exit with Ctrl-A then Ctrl-X. `PORT=` overrides `/dev/ttyACM0` |
| `make wifi-test` | Flash `wifi_ble`, join a network, then test ping and TCP. See [Testing](docs/testing.md#wi-fi-connection-test) |
| `make menuconfig APP=<app>` | Edit the Kconfig for an existing build |
| `make build-all` | Build every named application |
| `make clean` | Delete all build output |

## Applications

| `APP` | Source | What it does |
|---|---|---|
| `ble_shell` | `apps/ble_shell` | **Default.** Zephyr console over BLE, with Wi-Fi commands, logs and status messages |
| `wireless_uart` | `apps/wireless_uart` | Bridges the J10 UART to a BLE client. BLE only |
| `wifi_ble` | `apps/wifi_ble` | Wi-Fi shell on J10, plus a BLE characteristic reporting the Wi-Fi status |
| `blinky` | `apps/blinky` | Blinks the green LED and logs each change to J10 |
| `wifi` | Zephyr `samples/net/wifi/shell` | Upstream Wi-Fi shell sample |
| `ble` | Zephyr `samples/bluetooth/peripheral` | Upstream BLE peripheral sample |

`APP` can also be a path to any Zephyr application. Build output goes to `build/<app>/`.

## Documentation

- [BLE console](docs/ble-console.md): using the board over BLE
- [Architecture](docs/architecture.md): how the repo and the BLE console work, and how
  to add an application
- [Testing](docs/testing.md): what has been tested, known issues, and how to retest
- [Troubleshooting](docs/troubleshooting.md): problems we've hit and how to fix them

## Third-party firmware and tools

- **Radio firmware:** `make blobs` downloads NXP's RW61x firmware, covered by the NXP
  software license in
  `modules/hal/nxp/zephyr/blobs/license/LA_OPT_NXP_Software_License.txt` in the
  workspace. The firmware isn't stored in this repo, but the build links it into each
  Wi-Fi or BLE image, so check that license before distributing images.
- **J-Link:** the software is covered by SEGGER's license, which you accept when you
  download it.

## License

The code and documentation in this repo are licensed under the
[Apache License 2.0](LICENSE). That doesn't cover the third-party firmware and tools above,
or Zephyr and its modules, which have their own licenses.
