# Android makefile for audio kernel modules
MY_LOCAL_PATH := $(call my-dir)

ifeq ($(call is-board-platform-in-list,msm8953 sdm845 sdm670 sdm660 qcs605 msmnile $(MSMSTEPPE) $(TRINKET)),true)
UAPI_OUT := $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/include

$(shell mkdir -p $(UAPI_OUT)/linux;)
$(shell mkdir -p $(UAPI_OUT)/sound;)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/ipc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/dsp/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/dsp/codecs/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/soc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/wcd934x/Module.symvers)

include $(MY_LOCAL_PATH)/include/uapi/Android.mk
include $(MY_LOCAL_PATH)/ipc/Android.mk
include $(MY_LOCAL_PATH)/dsp/Android.mk
include $(MY_LOCAL_PATH)/dsp/codecs/Android.mk
include $(MY_LOCAL_PATH)/soc/Android.mk
include $(MY_LOCAL_PATH)/asoc/Android.mk
include $(MY_LOCAL_PATH)/asoc/codecs/Android.mk
include $(MY_LOCAL_PATH)/asoc/codecs/wcd934x/Android.mk
endif

ifeq ($(call is-board-platform-in-list, atoll),true)
UAPI_OUT := $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/include

$(shell mkdir -p $(UAPI_OUT)/linux;)
$(shell mkdir -p $(UAPI_OUT)/sound;)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/ipc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/dsp/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/dsp/codecs/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/soc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/asoc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/asoc/codecs/Module.symvers)

include $(MY_LOCAL_PATH)/4.0/include/uapi/Android.mk
include $(MY_LOCAL_PATH)/4.0/ipc/Android.mk
include $(MY_LOCAL_PATH)/4.0/dsp/Android.mk
include $(MY_LOCAL_PATH)/4.0/dsp/codecs/Android.mk
include $(MY_LOCAL_PATH)/4.0/soc/Android.mk
include $(MY_LOCAL_PATH)/4.0/asoc/Android.mk
include $(MY_LOCAL_PATH)/4.0/asoc/codecs/Android.mk
endif

ifeq ($(call is-board-platform-in-list,sdm670 msmnile),true)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/aqt1000/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/aqt1000/Android.mk
endif

ifeq ($(call is-board-platform-in-list, $(MSMSTEPPE) $(TRINKET)),true)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/bolero/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/bolero/Android.mk
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/wcd937x/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/wcd937x/Android.mk
endif

ifeq ($(call is-board-platform-in-list, atoll),true)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/asoc/codecs/bolero/Module.symvers)
include $(MY_LOCAL_PATH)/4.0/asoc/codecs/bolero/Android.mk
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/asoc/codecs/wcd937x/Module.symvers)
include $(MY_LOCAL_PATH)/4.0/asoc/codecs/wcd937x/Android.mk
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/4.0/asoc/codecs/wcd938x/Module.symvers)
include $(MY_LOCAL_PATH)/4.0/asoc/codecs/wcd938x/Android.mk
endif

ifeq ($(call is-board-platform-in-list,msm8953 sdm670 sdm660 qcs605),true)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/sdm660_cdc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/msm_sdw/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/sdm660_cdc/Android.mk
include $(MY_LOCAL_PATH)/asoc/codecs/msm_sdw/Android.mk
endif

ifeq ($(call is-board-platform-in-list,msmnile),true)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/wcd9360/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/wcd9360/Android.mk
endif
