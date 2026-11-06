PIO  ?= $(HOME)/.platformio/penv/bin/platformio
PORT ?= /dev/cu.usbserial-0001
DEMO_PORT ?= 8000

.PHONY: test build flash-hub flash-web flash-node flash-bench erase-hub backup sd-card sounds monitor demo clean

HUB_URL ?= http://192.168.4.1

test:                 ## unit tests on the Mac (game engine, arbiter, tuning)
	$(PIO) test -e native

build:                ## compile every firmware
	$(PIO) run -e node -e hub -e bench

flash-hub:            ## hub firmware + web UI (history and settings are kept)
	$(PIO) run -e hub -t upload --upload-port $(PORT)
	$(PIO) run -e hub -t uploadfs --upload-port $(PORT)

flash-web:            ## web UI only: writes the `spiffs` partition, never `userdata`
	$(PIO) run -e hub -t uploadfs --upload-port $(PORT)

erase-hub:            ## DANGER: wipes the whole hub flash, including history and settings
	@read -p "Erase ALL hub data (history, settings)? type yes: " a && [ "$$a" = yes ]
	$(PIO) run -e hub -t erase --upload-port $(PORT)

backup:               ## download a backup from the hub over Wi-Fi: make backup PIN=1234
	@test -n "$(PIN)" || (echo "usage: make backup PIN=<admin pin>" && exit 1)
	curl -fsS -o target-range-backup-$$(date +%Y%m%d-%H%M).jsonl "$(HUB_URL)/api/backup?pin=$(PIN)"
	@ls -l target-range-backup-*.jsonl | tail -1

flash-node:           ## mannequin firmware (set the JP1 jumper for mannequin 2)
	$(PIO) run -e node -t upload --upload-port $(PORT)

flash-bench:          ## bench-test firmware (no radio)
	$(PIO) run -e bench -t upload --upload-port $(PORT)

monitor:
	$(PIO) device monitor -p $(PORT) -b 115200

demo:                 ## phone UI against the in-browser simulated hub (plays sd-card/ hit sounds)
	@echo "open http://localhost:$(DEMO_PORT)/web/?demo"
	python3 -m http.server $(DEMO_PORT)

sounds:               ## regenerate the placeholder sounds (countdown + sd-card hit sounds)
	python3 tools/make_default_countdown.py
	python3 tools/make_test_hit_sounds.py

sd-card:              ## copy hit sounds to a mannequin microSD: make sd-card SD=/Volumes/NAME
	@test -n "$(SD)" && test -d "$(SD)" || (echo "usage: make sd-card SD=/Volumes/<card name>" && exit 1)
	mkdir -p "$(SD)/01"
	cp sd-card/01/*.mp3 "$(SD)/01/"
	dot_clean -m "$(SD)"
	@echo "copied $$(ls sd-card/01/*.mp3 | wc -l | tr -d ' ') sounds; set 'จำนวนไฟล์เสียงยิงโดน' to match in the admin page"

clean:
	$(PIO) run -t clean
	rm -rf data/www
