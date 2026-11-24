# Convenience wrappers around scripts/. `make help` lists targets.
# Override the preset with e.g. `make run PRESET=Debug`.

PRESET ?= Release

.PHONY: help setup build run run-nobuild test apply export sync clean

help:
	@echo "make setup        One-time machine/clone setup (brew deps, submodule, Rust, hooks)"
	@echo "make build        Incremental build ($(PRESET))"
	@echo "make run          Build + launch Superbird ($(PRESET))"
	@echo "make run-nobuild  Launch the last build"
	@echo "make test         Fast Superbird unit tests + settings JS syntax check"
	@echo "make apply        Reset upstream/ and re-apply patches + overlay (discards upstream/ edits!)"
	@echo "make export       Export upstream/ commits back into patches/"
	@echo "make sync         Test the patch stack against latest Ladybird master"
	@echo "make clean        Delete the $(PRESET) build dir (keeps vcpkg dependency cache)"

setup:
	./scripts/setup.sh

build:
	./scripts/build.sh $(PRESET)

run:
	./scripts/run.sh $(PRESET)

run-nobuild:
	SUPERBIRD_NO_BUILD=1 ./scripts/run.sh $(PRESET)

test:
	./scripts/ensure-applied.sh
	CXX=$${CXX:-clang++} ./scripts/test-unit.sh
	node --check upstream/Base/res/ladybird/about-pages/settings/tabs.js
	node --check upstream/Base/res/ladybird/about-pages/settings/superbird.js

apply:
	./scripts/apply.sh

export:
	./scripts/export-patches.sh

sync:
	./scripts/sync-upstream.sh

clean:
	rm -rf upstream/Build/$(shell echo $(PRESET) | tr '[:upper:]' '[:lower:]')
