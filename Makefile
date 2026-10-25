# SPDX-License-Identifier: GPL-2.0
KDIR ?= /lib/modules/$(shell uname -r)/build

TOOL_CFLAGS ?= -O2 -g -Wall -Wextra
# Static, so the same binary runs inside any container image.
TOOL_LDFLAGS ?= -static

all: module tools

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/module W=1 modules

tools: tools/rzio_test

tools/rzio_test: tools/rzio_test.c include/rzio.h
	$(CC) $(TOOL_CFLAGS) -Iinclude -o $@ $< $(TOOL_LDFLAGS)

# Boot the host kernel in a VM, load rzio, run the smoke test, unload.
vm-test:
	scripts/vm.sh test

vm-shell:
	scripts/vm.sh shell

# Kernel coding style.
check:
	$(KDIR)/scripts/checkpatch.pl --no-tree --strict -f module/rzio.c include/rzio.h

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/module clean
	rm -f tools/rzio_test

.PHONY: all module tools vm-test vm-shell check clean
