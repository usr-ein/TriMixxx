# ---------------------------------------------------------------------------
# OneButton ring node firmware (CH32V003). Built with ch32fun, which lives in
# a git submodule at the repo root (../../../ch32fun). On a fresh clone the
# rule below auto-runs `git submodule update --init` so `make` just works.
# ---------------------------------------------------------------------------
REPO_ROOT := ../../..
CH32FUN   := $(REPO_ROOT)/ch32fun/ch32fun

ifeq ($(wildcard $(CH32FUN)/ch32fun.mk),)
$(info [setup] ch32fun submodule not present - running 'git submodule update --init ch32fun')
$(shell git -C $(REPO_ROOT) submodule update --init ch32fun 1>&2)
endif

TARGET     := onebutton_node
TARGET_MCU ?= CH32V003

# Default target builds the binary only (no flashing).
# `make flash` flashes via minichlink (see README for the Ardulink command).
all : $(TARGET).bin

include $(CH32FUN)/ch32fun.mk

# This firmware uses CSR instructions (csrci/csrsi mstatus for irq mask/unmask).
# Modern GCC (e.g. xPack 15) split CSR ops into the "zicsr" extension, which
# ch32fun's default -march=rv32ec doesn't include. Re-add it (last -march wins;
# -mabi=ilp32e and codegen are unchanged).
CFLAGS += -march=rv32ec_zicsr

flash : cv_flash
clean : cv_clean
