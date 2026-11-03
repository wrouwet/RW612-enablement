# BLE console

`apps/ble_shell` makes a BLE connection the board's console, using NXP's Wireless UART
profile. It's the default application, so `make flash` installs it.

## Clients

| Client | Output from the board | Sending commands |
|---|---|---|
| NXP FRDM-MCXW23 board running NXP's `w_uart` example, used from a serial terminal in VS Code on Windows | Tested | Tested |
| NXP IoT Toolbox on a phone, Wireless UART screen | Tested | Not working: we never found a way to type in the app |

With the FRDM-MCXW23, the terminal only sees a serial port, and that board handles BLE. It
runs NXP's
[Wireless UART example](https://github.com/nxp-mcuxpresso/mcuxsdk-middleware-bluetooth-host/tree/main/examples/w_uart)
from the MCUXpresso SDK, which implements both sides of the profile.

A client has to offer the Wireless UART service itself, not just connect to the board's,
because the board writes its output to the client's characteristic. See
[How the BLE console works](architecture.md#how-the-ble-console-works). A generic BLE
serial app may not do this, and the board reports `Peer has no Wireless UART service` on
J10 if so.

## Connect

1. Disconnect any other device from the board. It allows one BLE connection at a time,
   and devices that compete for it make the link drop repeatedly.
2. Connect your client to `NXP_WU`.
3. Wait for the `rw612:~$` prompt and the greeting:

   ```
   RW612 console on BLE, Zephyr 4.5.0, up 815 s. Type help for commands.
   ```

## Commands

Each line you send runs as one command.

| Command | What it does |
|---|---|
| `help` | List the available commands |
| `kernel version` | Show the Zephyr version |
| `kernel uptime` | Show the time since boot |
| `wifi scan` | List nearby networks |
| `wifi connect -s <SSID> -k <type> -p <password>` | Join a network |
| `wifi status` | Show the Wi-Fi connection details |
| `wifi disconnect` | Leave the network |
| `net iface` | Show the IP address, gateway and DHCP details |
| `net ping <address>` | Ping an address |

For `wifi connect`, `-k` selects the security type:

| `-k` | Security |
|---|---|
| `0` | Open (leave out `-p`) |
| `1` | WPA2-PSK |
| `3` | WPA3-SAE |

Put a network name containing spaces in double quotes, for example
`wifi connect -s "My Network" -k 3 -p <password>`.

`wifi scan` has shown at most 10 results per scan in testing, so a run may not list every
nearby network.

## Status messages

The console reports the Wi-Fi status every 30 seconds, and immediately when it changes:

```
Wi-Fi: not connected (DISCONNECTED)
Wi-Fi: joined network, waiting for an IP address
Wi-Fi: connected to <SSID>, 5GHz channel 161, RSSI -35 dBm, IP <address>
Wi-Fi: connection failed (status <code>)
Wi-Fi: disconnected (status <code>)
```

Log lines also carry a timestamp and a prefix such as `<inf> ble_shell:`.

## What J10 shows

The USB serial port on J10 mirrors the logs and status messages, and it's the only place
to see boot messages and anything logged while no client is connected. It's output only
in this app: the shell itself is on BLE, so you can't type commands in `make monitor`.

Only one program should read the port at a time.

## Security

The console has **no pairing or encryption**, a deliberate choice for bench use:

- Any nearby BLE device can connect when no other device is connected, then run any shell
  command.
- Wi-Fi passwords you type cross the air unencrypted.
- Shell history is turned off so that passwords aren't kept in RAM after use. In a test
  with a dummy password, it didn't appear in the BLE output or on J10.

Add pairing before using the console outside a test setup, and use a throwaway network
password until then.

## Limitations

- One BLE connection at a time.
- Output produced while no client is connected is discarded rather than saved for later.
- The Wi-Fi network isn't remembered, so you have to join again after a reset.

See [Testing](testing.md) for known issues.
