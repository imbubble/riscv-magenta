# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MDI_BIN := $(BUILDDIR)/mdi.bin
GEN_HEADER_DIR := $(BUILDDIR)/gen/include
MDI_HEADER_DIR := $(GEN_HEADER_DIR)/mdi
MDI_HEADER := $(MDI_HEADER_DIR)/mdi-defs.h

# This is a big hammer, but we need to make sure $(MDI_HEADER) is generated
# before it is included by any source files
TARGET_MODDEPS += $(MDI_HEADER)
GLOBAL_INCLUDES += $(GEN_HEADER_DIR)

MDIGEN := $(BUILDDIR)/tools/mdigen

# add "MDI_" prefix and make header file symbols uppercase
MDI_HEADER_OPTS := -p "MDI_" -u

$(MDI_BIN): $(MDIGEN) $(MDI_SRCS) $(MDI_DEPS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MDIGEN) -o $@ $(MDI_SRCS)

$(MDI_HEADER): $(MDIGEN) $(MDI_SRCS) $(MDI_DEPS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MDIGEN) $(MDI_SRCS) -h $@ $(MDI_HEADER_OPTS)

GENERATED += $(MDI_BIN) $(MDI_HEADER)
ADDITIONAL_BOOTDATA_ITEMS += $(MDI_BIN)
EXTRA_BUILDDEPS += $(MDI_BIN) $(MDI_HEADER)
