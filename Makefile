IDF_PATH ?= $(HOME)/esp/esp-idf
SERIAL_PORT ?= /dev/cu.usbserial-10
ADB_DEVICE ?= $(shell adb devices | grep -v List | head -1 | awk '{print $$1}')

# ── Setup ──

.PHONY: setup
setup: setup-firmware setup-app setup-cloud

.PHONY: setup-firmware
setup-firmware:
	. $(IDF_PATH)/export.sh

.PHONY: setup-app
setup-app:
	cd app && flutter pub get

.PHONY: setup-cloud
setup-cloud:
	cd cloud && bun install

# ── Firmware ──

.PHONY: firmware
firmware:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py build

.PHONY: flash
flash:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py -p $(SERIAL_PORT) flash

.PHONY: monitor
monitor:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py -p $(SERIAL_PORT) monitor

.PHONY: flash-monitor
flash-monitor:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py -p $(SERIAL_PORT) flash monitor

.PHONY: firmware-clean
firmware-clean:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py fullclean

.PHONY: erase
erase:
	. $(IDF_PATH)/export.sh && cd firmware && idf.py -p $(SERIAL_PORT) erase-flash

# ── Flutter App ──

.PHONY: app
app:
	cd app && flutter build apk --release

.PHONY: dev
dev:
	cd app && flutter run

.PHONY: install
install:
	adb -s $(ADB_DEVICE) install -r app/build/app/outputs/flutter-apk/app-release.apk

.PHONY: app-analyze
app-analyze:
	cd app && dart analyze lib/

# ── Cloud ──

.PHONY: deploy
deploy:
	cd cloud && bun run deploy

.PHONY: cloud-dev
cloud-dev:
	cd cloud && bun run dev

.PHONY: migrate
migrate:
	cd cloud && bun run db:migrate

# ── Debug Server ──

.PHONY: adb-forward
adb-forward:
	adb -s $(ADB_DEVICE) forward tcp:8350 tcp:8350

.PHONY: db-count
db-count: adb-forward
	@curl -s localhost:8350/db/count | python3 -m json.tool

.PHONY: db-recent
db-recent: adb-forward
	@curl -s "localhost:8350/db/recent?limit=10" | python3 -m json.tool 2>/dev/null || curl -s "localhost:8350/db/recent?limit=10"

.PHONY: db-export
db-export: adb-forward
	curl -o adv350.db localhost:8350/db/export
	@echo "Exported to adv350.db"

.PHONY: raw-list
raw-list: adb-forward
	@curl -s localhost:8350/raw/list | python3 -m json.tool 2>/dev/null || curl -s localhost:8350/raw/list

# ── Release ──

.PHONY: release
release:
ifndef VERSION
	$(error VERSION is required. Usage: make release VERSION=0.3.0)
endif
	git push
	gh release create v$(VERSION) firmware/build/can_sniffer.bin \
		--title "v$(VERSION)" \
		--generate-notes

# ── Combo ──

.PHONY: build-all
build-all: firmware app

.PHONY: deploy-all
deploy-all: app install deploy
