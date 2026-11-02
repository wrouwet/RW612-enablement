#!/usr/bin/env bash
# Installs the Ubuntu packages and udev rule the project needs. Run with sudo.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo $0" >&2
    exit 1
fi

packages=(
    git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget
    python3-dev python3-venv python3-tk xz-utils file make gcc gcc-multilib
    g++-multilib libsdl2-dev libmagic1t64 picocom libusb-1.0-0 libhidapi-hidraw0
)

apt-get update
apt-get install --no-install-recommends -y "${packages[@]}"

# Gives the logged-in user access to NXP (1fc9) and SEGGER (1366) debug probes,
# including their serial ports, without sudo or a group change.
cat > /etc/udev/rules.d/70-rw612-debug-probes.rules <<'EOF'
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1fc9", TAG+="uaccess"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1366", TAG+="uaccess"
EOF
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --subsystem-match=tty --subsystem-match=hidraw

echo "Host dependencies installed."
