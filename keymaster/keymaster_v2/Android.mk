# Copyright (C) 2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_KEYMASTER_VERSION),2)

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := keystore.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := -Wall -Wextra -Werror -Wunused
LOCAL_SRC_FILES := keymaster_na.cpp logger.cpp

LOCAL_HEADER_LIBRARIES := libhardware_headers libutils_headers
LOCAL_SHARED_LIBRARIES := \
    liblog   \
    libteec

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include \
    $(TOP)/system/core/include \
    $(TOP)/system/keymaster/include \
    $(TOP)/hardware/aw/optee_client/public

include $(BUILD_SHARED_LIBRARY)
endif
