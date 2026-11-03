# Troubleshooting

These are problems we've actually hit on this project.

## BLE

### The connection keeps dropping and coming back

Check for a second device connecting to the board, such as a bridge board, laptop or phone. The
board allows one connection, and devices that compete for it cause exactly this: one device
says it's connected while the console shows connects and disconnects. Disconnect or turn off
Bluetooth on the other device.

A rare, single drop with reason `0x08` is a separate [known issue](testing.md#known-issues).

### `NXP_WU` doesn't appear in the app

- The board stops advertising while a device is connected, so disconnect any other device.
- Check that the board is running a BLE app. Run `make flash`, or check for `Advertising...`
  on J10.
- If you've been using a laptop to scan, a short scan can miss the board. Allow at least
  15 seconds.

### I can't type in IoT Toolbox

We never found a way to type in the app's Wireless UART screen on our test phone, although it
displayed the board's output. Use a USB-to-BLE UART bridge and a serial terminal instead. See
[Clients](ble-console.md#clients).

### A laptop shows the board under an old name

BlueZ caches device names by address, so a laptop may show the name of a previously flashed
app, such as `RW612 WiFi+BLE`. Clear it with `bluetoothctl remove <address>`. This doesn't
affect the board.

### The first message from the board arrives garbled on a laptop

We saw this once when using `bluetoothctl` as the peer: the first message had the right
length but the wrong contents, as BlueZ was setting up its socket. Sending one throwaway byte
first avoided it, and later transfers were byte-exact. The cause wasn't proven.

## Wi-Fi

### The board joins a network but nothing gets through

Try a network you know allows client traffic, such as a phone hotspot, and see the
[known issue](testing.md#known-issues). `net iface` shows whether it got an address and
gateway, and `net arp` shows whether it can resolve the gateway.

### Commands don't work in `make monitor`

In `ble_shell`, the shell is on BLE, and J10 only shows output. To use a Wi-Fi shell on J10,
run `make flash APP=wifi_ble`.

### `board_type` lines appear

These come from the Wi-Fi driver at boot, and we've also seen them repeated while joining a
network. They've appeared on working connections.

## Flashing and USB

### Flashing fails or J-Link isn't found

Check that J-Link is extracted to `.tools/JLink` in the workspace, as in the
[README](../README.md#quick-start), and that USB is connected to J10. `lsusb` should list
`SEGGER J-Link`.

### Permission denied on `/dev/ttyACM0` or the probe

Run `sudo scripts/install-host-deps.sh`, which installs the udev rule, then unplug and
replug the board.

### The board disappears after the laptop sleeps

After the laptop slept overnight, the board was missing from `lsusb` and there was no
`/dev/ttyACM0`. Replugging the USB cable fixed it. The board restarted and resumed
advertising.

### `device reports readiness to read but returned no data`

This comes from a serial program when the USB connection drops, or when two programs read
the port at once. Close the other program and check that the board is still connected.

## Logs

### Timestamps don't start at zero after flashing

Log timestamps can continue from a large value after flashing, such as `00:14:41`, and don't
mean the board has been running that long.

### A serial capture looks cut off

Tools that read in short bursts can record one log line in two pieces, so don't filter a
capture line by line before reading it. We once mistook this for a console stall.

## Setup

### A long command pasted into a terminal only partly runs

This happened when pasting the package list into a terminal: only the part before the line
wrap ran. `scripts/install-host-deps.sh` avoids it.

### Zephyr's docs warn about newer Python versions

Zephyr recommends Python 3.12 and warns that newer versions may fail. `make setup` installs
3.12 inside the workspace, so the system's Python version doesn't matter.
