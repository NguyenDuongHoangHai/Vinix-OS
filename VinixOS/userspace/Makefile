# ==============================================================================
# VinixOS Userspace Master Makefile
# ==============================================================================

MAKEFLAGS += --no-print-directory

# shell must always build first — the kernel embeds shell.bin as its
# initial user-space payload. The rest are external ELFs users copy
# onto the FAT32 rootfs and exec through the shell.
APPS = shell uname hello

.PHONY: all lib apps clean help $(addprefix build-, $(APPS))

all: build_lib apps

build_lib:
	@echo "========================================"
	@echo " Building Userspace Library (librefarm) "
	@echo "========================================"
	@$(MAKE) -C lib

apps: build_lib $(addprefix build-, $(APPS))

$(addprefix build-, $(APPS)): build-%: build_lib
	@echo "========================================"
	@echo " Building Application: $*               "
	@echo "========================================"
	@$(MAKE) -C apps/$*

lib: build_lib

clean:
	@echo "========================================"
	@echo " Cleaning Userspace Build Artifacts     "
	@echo "========================================"
	@rm -rf build/
	@echo "  Clean complete."

help:
	@echo "VinixOS Userspace Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make            # Build library + all apps (shell, uname, hello, ...)"
	@echo "  make build-X    # Build just app X"
	@echo "  make clean      # Clean all build artifacts"
	@echo ""
	@echo "Available apps: $(APPS)"
