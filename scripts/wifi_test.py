#!/usr/bin/env python3
"""Connects the board running apps/wifi_ble to a Wi-Fi network and tests the link.

Prompts for the credentials so they stay out of shell history and logs. The board
stays connected afterwards. The password is held only in the board's RAM.
"""

import argparse
import getpass
import re
import sys
import time

import serial

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
SECURITY = {"open": 0, "wpa2": 1, "wpa3": 3}
CONNECT_TIMEOUT_S = 60
PING_COUNT = 5


class Board:
    def __init__(self, port, log_path, secret):
        self.serial = serial.Serial(port, 115200, timeout=0.1)
        self.log = open(log_path, "w", encoding="utf-8")
        self.secret = secret

    def send(self, line):
        # The shell's receive path is slow, so pace the characters.
        for char in line + "\r":
            self.serial.write(char.encode())
            time.sleep(0.002)

    def read_until(self, pattern, timeout):
        """Returns output up to the first regex match, or None on timeout."""
        text = ""
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self.serial.read(4096).decode(errors="replace")
            if data:
                text += data
                match = re.search(pattern, ANSI.sub("", text))
                if match:
                    self.record(text)
                    return ANSI.sub("", text)
        self.record(text)
        return None

    def run(self, command, timeout=3):
        self.record(f"$ {command}\n")
        self.send(command)
        return self.read_until(r"(?!x)x", timeout) or self.last

    def record(self, text):
        clean = ANSI.sub("", text).replace("\r", "")
        if self.secret:
            clean = clean.replace(self.secret, "***")
        self.last = clean
        self.log.write(clean)
        self.log.flush()
        print(clean, end="", flush=True)


def quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ask(prompt, secret=False):
    if not sys.stdin.isatty():
        return sys.stdin.readline().rstrip("\n")
    return getpass.getpass(prompt) if secret else input(prompt)


def check(results, name, passed):
    results.append((name, passed))
    return passed


def ping(board, results, name, address):
    output = board.run(f"net ping -c {PING_COUNT} {address}", timeout=PING_COUNT + 5)
    replies = len(re.findall(r"bytes from", output))
    print(f"\n{name}: {replies}/{PING_COUNT} replies")
    check(results, f"{name} ({replies}/{PING_COUNT} replies)", replies > 0)


def tcp_connect(board, results, address, port):
    output = board.run(f"net tcp connect {address} {port}", timeout=12)
    connected = "TCP connected" in output
    board.run("net tcp close", timeout=1)
    print()
    check(results, f"TCP connect to {address}:{port}", connected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    ssid = ask("Wi-Fi network name: ")
    security = ask("Security [wpa2/wpa3/open, default wpa2]: ") or "wpa2"
    if security not in SECURITY:
        sys.exit(f"Unknown security type: {security}")
    password = "" if security == "open" else ask("Password (hidden): ", secret=True)

    board = Board(args.port, args.log, password)
    results = []

    command = f"wifi connect -s {quote(ssid)} -k {SECURITY[security]}"
    if password:
        command += f" -p {quote(password)}"

    board.run("", timeout=1)
    board.run("shell echo off", timeout=1)
    board.record("$ wifi connect (arguments hidden)\n")
    board.send(command)
    output = board.read_until(
        r"Wi-Fi status: (connected \S+|connect failed)", CONNECT_TIMEOUT_S
    )
    board.run("shell echo on", timeout=1)

    if not check(results, "Associate and get an IP address", bool(output and "connected " in output)):
        report(results)
        return 1

    board.run("wifi status", timeout=2)
    print()
    iface = board.run("net iface", timeout=2)
    gateway = re.search(r"IPv4 gateway\s*:\s*(\d+\.\d+\.\d+\.\d+)", iface)

    if check(results, "DHCP supplied a gateway", gateway is not None):
        ping(board, results, "Ping gateway", gateway.group(1))
    ping(board, results, "Ping internet (1.1.1.1)", "1.1.1.1")
    tcp_connect(board, results, "1.1.1.1", 80)
    tcp_connect(board, results, "1.1.1.1", 443)

    report(results)
    return 0 if all(passed for _, passed in results) else 1


def report(results):
    print("\nResults:")
    for name, passed in results:
        print(f"  {'PASS' if passed else 'FAIL'}  {name}")


if __name__ == "__main__":
    sys.exit(main())
