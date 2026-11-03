# Testing

## What has been tested

All testing so far is manual, on one board. There are no automated tests. Commands were
sent from a serial terminal in VS Code on Windows, through an NXP FRDM-MCXW23 board running
NXP's `w_uart` example as a USB-to-BLE UART bridge. Output was also received on one phone with NXP's IoT Toolbox app.

| Feature | App | Tested on hardware |
|---|---|---|
| Console commands through the FRDM-MCXW23 bridge, with replies | `ble_shell` | Yes |
| Join a Wi-Fi network over the BLE console and get an IP address (WPA3, 5 GHz) | `ble_shell` | Yes |
| Wi-Fi status every 30 seconds while connected | `ble_shell` | Yes, about 7 minutes |
| Ping and TCP over Wi-Fi (WPA3, 5 GHz) | `wifi_ble` | Yes, not yet in `ble_shell` |
| Wi-Fi and BLE active together | `wifi_ble`, `ble_shell` | Yes, for minutes rather than hours |
| UART bridge, both directions | `wireless_uart` | Yes, with the FRDM-MCXW23 bridge, and a 2 KB transfer with `bluetoothctl` on Linux |
| UART bridge, output received in IoT Toolbox on a phone | `wireless_uart` | Yes |
| Sending from IoT Toolbox on a phone | | Not working, see known issues |
| LED blink and J10 console | `blinky` | Yes |
| Traffic on 2.4 GHz with WPA2 | | Not verified, see known issues |
| Flashing with LinkServer (`RUNNER=linkserver`) | | Not tested. The board's probe runs J-Link firmware |
| Setup and all builds from a fresh clone | | Yes, on the same Ubuntu 26.04 machine, with host packages already installed. Not on a new machine, and `install-host-deps.sh` wasn't rerun |
| Long-term stability, throughput and range | | Not tested |

## Known issues

- **No sending from IoT Toolbox:** the app displayed the board's output, but we never found a
  way to type in it on our test phone. The cause may be the app or the board, and it wasn't
  investigated further because the FRDM-MCXW23 bridge works.
- **Occasional BLE drops:** the link can time out (reason `0x08`) with a client connected,
  even with the 4 second timeout. One drop was recorded in a 30 minute capture, and the
  client reconnected. It isn't known whether another device was also trying to connect at
  the time.
- **Errors while joining Wi-Fi:** `[wlcm] Error: error: scan failed` and repeated
  `board_type` lines appeared around one connection attempt, and the board still joined
  the network afterwards. The cause is unknown.
- **No traffic on one network:** on a WPA2, 2.4 GHz network, the board joined and got an IP
  address, and could resolve the gateway through ARP, but all ping and TCP attempts failed.
  Network filtering is the most likely cause, but a board problem on that band or security
  type isn't ruled out. Testing on a phone hotspot set to 2.4 GHz and WPA2 would separate
  the two.

## Wi-Fi connection test

`make wifi-test` flashes `wifi_ble`, prompts for the network name, security type and
password, then checks association, DHCP, ping and TCP. Results go to the screen and to
`build/wifi-test.log`. The password stays out of the log.

The board stays connected to the network afterwards, but it's now running `wifi_ble`
rather than the console, so run `make flash` to return to `ble_shell`.

A failed ping isn't always a board problem, because some networks block it. A connection
attempt that never gets a result takes the full 60 second timeout to fail.

## Testing BLE with bluetoothctl

On Linux, `bluetoothctl` can act as the client, which is useful for scripted or byte-exact
checks. The board allows one connection, so disconnect other clients first, and disconnect
when you've finished, or it will compete with them.

It has to offer the Wireless UART service itself, because the board discovers it:

```
menu gatt
register-service 01ff0100-ba5e-f4ee-5ca1-eb1e5e4b1ce0
yes
register-characteristic 01ff0101-ba5e-f4ee-5ca1-eb1e5e4b1ce0 write-without-response
0x00
register-application
back
scan on
```

Once the scan has found the board, connect and send a command as hex bytes. This one is
`help` followed by `\r\n`:

```
scan off
connect C0:95:DA:01:83:D4
menu gatt
select-attribute /org/bluez/hci0/dev_C0_95_DA_01_83_D4/service0010/char0011
write "0x68 0x65 0x6c 0x70 0x0d 0x0a" 0 command
```

The address and attribute path are this board's, so yours will differ. Use
`list-attributes <address>` to find the path, and pick the path under the device rather
than the one under `/org/bluez/app`. Output from the board appears in `bluetoothctl` as
hex dumps of writes to the local characteristic.

## Watching the board without using BLE

J10 mirrors the logs in `ble_shell`, so `make monitor` shows what the board is doing without
taking up the BLE connection.

To keep a laptop awake during a long capture:

```bash
systemd-inhibit --what=sleep --why="Console capture" make monitor
```
