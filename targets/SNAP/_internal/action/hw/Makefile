export ACTION_BUILDROOT=$(IMAGE_BUILD_DIR)/$(SNAP_TARGET)/action

vhd_srcs=$(wildcard *.vhd_source)
vhd_targets=$(addprefix $(ACTION_BUILDROOT)/,$(vhd_srcs))
vhd_files=$(vhd_targets:.vhd_source=.vhd)

ACTION_ROOT ?= $(PWD)/..
CONFIG_FILE  = $(_SNAP_ROOT)/.snap_config

hls_components  = $(shell find hls/ -maxdepth 1 -type d -name 'hls_*')

ifeq ($(DDRI_USED),)
export DDRI_USED = $(shell grep DDRI_USED $(CONFIG_FILE) | cut -d = -f 2 | tr -d '"')
endif
ifeq ($(NVME_USED),)
export NVME_USED = $(shell grep NVME_USED $(CONFIG_FILE) | cut -d = -f 2 | tr -d '"')
endif
ifeq ($(FPGACHIP),)
export FPGACHIP  = $(shell grep FPGACHIP $(CONFIG_FILE) | cut -d = -f 2 | tr -d '"')
endif

ifeq ($(DDRI_USED),TRUE)
DDRI_FILTER = "\-\- only for DDRI_USED!=TRUE"
else
DDRI_FILTER = "\-\- only for DDRI_USED=TRUE"
endif

ifeq ($(NVME_USED),TRUE)
NVME_FILTER = "\-\- only for NVME_USED!=TRUE"
else
NVME_FILTER = "\-\- only for NVME_USED=TRUE"
endif

.PHONY: clean vhdl $(hls_components) block_design image_info nvme_arbiter

all: vhd_files block_design

$(LOGS_DIR):
	@mkdir -p $(LOGS_DIR)

block_design: hls nvme_arbiter image_info $(LOGS_DIR)
	@echo "                        Generating Block Design"
	@make -C $(ACTION_ROOT)/ip/block_design ip > $(LOGS_DIR)/$(@F)_make.log; viv_ret=$$?; \
	if [ $${viv_ret} -ne 0 ]; then \
		echo "                        Error: please look into $(LOGS_DIR)/$(@F)_make.log"; exit -1; \
	fi

image_info: $(IMAGE_TARGET) $(LOGS_DIR)
	@echo "                        Generating Image Info"
	@make -C $(ACTION_ROOT)/ip/image_info ip > $(LOGS_DIR)/$(@F)_make.log; viv_ret=$$?; \
	if [ $${viv_ret} -ne 0 ]; then \
		echo "                        Error: please look into $(LOGS_DIR)/$(@F)_make.log"; exit -1; \
	fi

nvme_arbiter: $(LOGS_DIR)
	@echo "                        Generating NVMe Arbiter"
	@make -C $(ACTION_ROOT)/ip/nvme_arbiter ip > $(LOGS_DIR)/$(@F)_make.log; viv_ret=$$?; \
	if [ $${viv_ret} -ne 0 ]; then \
		echo "                        Error: please look into $(LOGS_DIR)/$(@F)_make.log"; exit -1; \
	fi

hls: $(hls_components)

$(hls_components): $(LOGS_DIR)
	@echo "                        Generating IP from HLS component $(@F)"
	@echo "Calling make -C $@ ip" > $(LOGS_DIR)/$(@F)_make.log
	@make -C $@ ip >> $(LOGS_DIR)/$(@F)_make.log; hls_ret=$$?; \
	if [ $${hls_ret} -ne 0 ]; then \
		echo "                        Error: please look into $(LOGS_DIR)/$(@F)_make.log"; exit -1; \
	fi

test: $(LOGS_DIR)
	@for hls_dir in ./hls/hls_*/; do \
	make -C $$hls_dir test; hls_ret=$$?; \
	if [ $${hls_ret} -ne 0 ]; then \
		echo "                        Error: Test of $$(basename $$hls_dir) failed"; exit -1; \
	fi \
	done

vhd_files: $(vhd_files)

$(ACTION_BUILDROOT):
	@mkdir -p $(ACTION_BUILDROOT)

$(vhd_files): $(vhd_srcs) $(ACTION_BUILDROOT)
	@echo "                        Generating $@"
	@grep -v $(DDRI_FILTER) $< | grep -v $(NVME_FILTER) > $@

clean:
	@$(RM) $(vhd_files)

	@for hls_dir in ./hls/hls_*/; do make -C $$hls_dir clean; done
	@for ip_dir in ../ip/*; do make -C $$ip_dir clean; done

vhdl:
