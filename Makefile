# Build, flash and monitor Zephyr applications for the NXP FRDM-RW612.
# Run `make help` for usage.

REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WS   := $(abspath $(REPO)/..)

BOARD  ?= frdm_rw612
APP    ?= blinky
PORT   ?= /dev/ttyACM0
RUNNER ?=

export PATH := $(WS)/.venv/bin:$(WS)/.tools/JLink:$(PATH)
export ZEPHYR_BASE := $(WS)/zephyr
export ZEPHYR_SDK_INSTALL_DIR := $(WS)/.tools/zephyr-sdk-1.0.1

# APP is a short name from this list, or a path to any Zephyr application.
APP_PATH_blinky := $(REPO)/apps/blinky
APP_PATH_wifi_ble := $(REPO)/apps/wifi_ble
APP_PATH_wireless_uart := $(REPO)/apps/wireless_uart
APP_PATH_wifi   := $(ZEPHYR_BASE)/samples/net/wifi/shell
APP_PATH_ble    := $(ZEPHYR_BASE)/samples/bluetooth/peripheral

APP_PATH  := $(or $(APP_PATH_$(APP)),$(abspath $(APP)))
BUILD_DIR := $(REPO)/build/$(notdir $(APP))

.DEFAULT_GOAL := help
.PHONY: help setup blobs build build-all flash monitor wifi-test menuconfig clean

help:
	@echo "First-time setup:"
	@echo "  sudo scripts/install-host-deps.sh   Install Ubuntu packages and udev rule"
	@echo "  make setup                          Install Python, west, Zephyr, toolchain"
	@echo "  make blobs                          Fetch the RW61x Wi-Fi and BLE firmware"
	@echo
	@echo "Daily use (APP=blinky|wifi_ble|wireless_uart|wifi|ble|<path>, default blinky):"
	@echo "  make build APP=wifi                 Build an application"
	@echo "  make flash APP=wifi                 Build and flash [RUNNER=jlink|linkserver]"
	@echo "  make monitor                        Open the serial console [PORT=$(PORT)]"
	@echo "  make wifi-test                      Flash wifi_ble, join a network, ping"
	@echo "  make menuconfig APP=wifi            Edit the Kconfig for a build"
	@echo "  make build-all                      Build every named application"
	@echo "  make clean                          Delete all build output"

setup:
	$(REPO)/scripts/bootstrap.sh

blobs:
	cd $(WS) && west blobs fetch hal_nxp --allow-regex 'rw61x/.*'

build:
	cd $(WS) && west build -b $(BOARD) -d $(BUILD_DIR) $(APP_PATH)

build-all:
	$(MAKE) build APP=blinky
	$(MAKE) build APP=wifi_ble
	$(MAKE) build APP=wireless_uart
	$(MAKE) build APP=wifi
	$(MAKE) build APP=ble

flash: build
	cd $(WS) && west flash -d $(BUILD_DIR) $(if $(RUNNER),--runner $(RUNNER))

monitor:
	picocom -b 115200 $(PORT)

wifi-test:
	$(MAKE) flash APP=wifi_ble
	mkdir -p $(REPO)/build
	sleep 5
	python3 $(REPO)/scripts/wifi_test.py --port $(PORT) --log $(REPO)/build/wifi-test.log

menuconfig:
	cd $(WS) && west build -d $(BUILD_DIR) -t menuconfig

clean:
	rm -rf $(REPO)/build
