#Android makefile to build kernel as a part of Android Build
PERL		= perl

ifeq ($(TARGET_PREBUILT_KERNEL),)
KERNEL_OUT := out/$(TARGET_DEVICE)/$(BUILD_TARGET)/obj
KERNEL_CONFIG := $(KERNEL_OUT)/.config
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/arm/boot/zImage
KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
KERNEL_MODULES_INSTALL := system
KERNEL_MODULES_OUT := $(TARGET_OUT)/lib/modules
KERNEL_IMG=$(KERNEL_OUT)/arch/arm/boot/Image

DTS_NAMES := msm8974
DTS_FILES = $(wildcard ./arch/arm/boot/dts/$(G2_DTS_TARGET)/*.dts)

DTS_FILE = $(lastword $(subst /, ,$(1)))
DTB_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%.dtb,$(call DTS_FILE,$(1))))
ZIMG_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%-zImage,$(call DTS_FILE,$(1))))
KERNEL_ZIMG = $(KERNEL_OUT)/arch/arm/boot/zImage
DTC = $(KERNEL_OUT)/scripts/dtc/dtc




define append-dtb
mkdir -p $(KERNEL_OUT)/arch/arm/boot;\
$(foreach DTS_NAME, $(DTS_NAMES), \
   $(foreach d, $(DTS_FILES), \
      $(DTC) -p 1024 -O dtb -o $(call DTB_FILE,$(d)) $(d); \
      cat $(KERNEL_ZIMG) $(call DTB_FILE,$(d)) > $(call ZIMG_FILE,$(d));))
endef

ifeq ($(TARGET_USES_UNCOMPRESSED_KERNEL),true)
$(info Using uncompressed kernel)
TARGET_PREBUILT_KERNEL := $(KERNEL_OUT)/piggy
else
TARGET_PREBUILT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)
endif

define mv-modules
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`;\
ko=`find $$mpath/kernel -type f -name *.ko`;\
for i in $$ko; do mv $$i $(KERNEL_MODULES_OUT)/; done;\
fi
endef

define clean-module-folder
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`; rm -rf $$mpath;\
fi
endef

PHONY += dtc

dtc:
	@echo KERNEL_OUT=$(KERNEL_OUT)
	@echo KERNEL_CONFIG=$(KERNEL_CONFIG)
	@echo TARGET_PREBUILT_INT_KERNEL=$(TARGET_PREBUILT_INT_KERNEL)
	@echo KERNEL_HEADERS_INSTALL=$(KERNEL_HEADERS_INSTALL)
	@echo KERNEL_MODULES_INSTALL=$(KERNEL_MODULES_INSTALL)
	@echo KERNEL_MODULES_OUT=$(KERNEL_MODULES_OUT)
	@echo KERNEL_IMG=$(KERNEL_IMG)

	@echo DTS_NAMES=$(DTS_NAMES)
	@echo DTS_FILES=$(DTS_FILES)
	@echo DTS_FILE=$(DTS_FILE)
	@echo DTB_FILE=$(DTB_FILE)
	@echo ZIMG_FILE=$(ZIMG_FILE)
	@echo KERNEL_ZIMG=$(KERNEL_ZIMG)
	@echo DTC=$(DTC)
	@echo KERNEL_IMG=$(KERNEL_IMG)
	@echo G2_DTS_TARGET=$(G2_DTS_TARGET)
	$(append-dtb)

endif


PHONY += kernel_config
kernel_config: $(KERNEL_OUT)
	$(MAKE) -C kernel O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-eabi- msm8974_sec_defconfig VARIANT_DEFCONFIG=msm8974_sec_hltedcm_defconfig SELINUX_DEFCONFIG=selinux_defconfig SELINUX_LOG_DEFCONFIG=selinux_log_defconfig TIMA_DEFCONFIG=tima_defconfig

PHONY += kernel_config_kdi
kernel_config_kdi: $(KERNEL_OUT)
	$(MAKE)  ARCH=arm CROSS_COMPILE=arm-eabi- msm8974_sec_defconfig VARIANT_DEFCONFIG=msm8974_sec_hltekdi_defconfig SELINUX_DEFCONFIG=selinux_defconfig SELINUX_LOG_DEFCONFIG=selinux_log_defconfig TIMA_DEFCONFIG=tima_defconfig

