PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

include $(L4DIR)/mk/Makeconf

vpath %.cc $(SRC_DIR)/ARCH-$(ARCH)

SYSTEMS         = arm-l4f
TARGET          = arm-vmm
SRC_CC          = gic.cc main.cc ram_ds.cc generic_guest.cc guest.cc
REQUIRES_LIBS   = libstdc++ libio-vbus libfdt
DEFAULT_RELOC   = 0xa0000000
SRC_CC_IS_CXX11 = y
CXXFLAGS       += -O3
PRIVATE_INCDIR  = $(SRC_DIR) $(SRC_DIR)/ARCH-$(ARCH)

include $(L4DIR)/mk/prog.mk

CXXFLAGS := $(filter-out -march%,$(CXXFLAGS)) -mcpu=cortex-a15
CXXFLAGS := $(filter-out -fno-strict-aliasing,$(CXXFLAGS))
