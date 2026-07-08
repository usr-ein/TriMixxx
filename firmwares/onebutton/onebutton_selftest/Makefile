# ---------------------------------------------------------------------------
# OneButton board bring-up self-test firmware (CH32V003). Built with ch32fun,
# which lives in a git submodule at the repo root (../../../ch32fun). On a
# fresh clone the rule below auto-runs `git submodule update --init`.
# ---------------------------------------------------------------------------
REPO_ROOT := ../../..
CH32FUN   := $(REPO_ROOT)/ch32fun/ch32fun

ifeq ($(wildcard $(CH32FUN)/ch32fun.mk),)
$(info [setup] ch32fun submodule not present - running 'git submodule update --init ch32fun')
$(shell git -C $(REPO_ROOT) submodule update --init ch32fun 1>&2)
endif

TARGET     := onebutton_selftest
TARGET_MCU ?= CH32V003

# Default target builds the binary only (no flashing).
# `make flash` flashes via minichlink (see README for the Ardulink command).
all : $(TARGET).bin

include $(CH32FUN)/ch32fun.mk

# Kept identical to the onebutton_node project so both build pipelines match.
# This firmware doesn't use CSR instructions, so zicsr is a no-op here (verified
# byte-identical output), but it keeps the flags in lockstep (last -march wins).
CFLAGS += -march=rv32ec_zicsr

flash : cv_flash
clean : cv_clean
