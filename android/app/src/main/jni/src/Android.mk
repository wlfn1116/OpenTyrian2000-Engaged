JNI_PATH := $(call my-dir)

# Build the shared src/ tree directly. Platform-specific files guard themselves.
LOCAL_PATH := $(JNI_PATH)/../../../../../../src

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_SRC_FILES := $(notdir $(wildcard $(LOCAL_PATH)/*.c))

LOCAL_C_INCLUDES := $(JNI_PATH)/../SDL2/include $(JNI_PATH)/../SDL2_net

# Keep character and floating-point behavior consistent with other netplay targets.
LOCAL_CFLAGS := -std=iso9899:1999 -O2 -fsigned-char -ffp-contract=off \
                -Wall -Wno-format-truncation -Wno-missing-field-initializers \
                -DNDEBUG -DTARGET_UNIX -DWITH_NETWORK -DTYRIAN_DIR=\"data\"

ifneq ($(OPENTYRIAN_COMMIT),)
LOCAL_CFLAGS += -DOPENTYRIAN_COMMIT=\"$(OPENTYRIAN_COMMIT)\"
endif

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_net

LOCAL_LDLIBS := -lm -llog

include $(BUILD_SHARED_LIBRARY)
