JNI_PATH := $(call my-dir)

# Build straight out of the shared src/ tree rather than copying it in, so the Android
# port never drifts from the other targets. Platform-specific files self-guard.
LOCAL_PATH := $(JNI_PATH)/../../../../../../src

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_SRC_FILES := $(notdir $(wildcard $(LOCAL_PATH)/*.c))

LOCAL_C_INCLUDES := $(JNI_PATH)/../SDL2/include $(JNI_PATH)/../SDL2_net

# -fsigned-char matches the engine's x86 assumptions and -ffp-contract=off keeps float
# results identical to the other platforms, which netplay and the replay fixtures require.
LOCAL_CFLAGS := -std=iso9899:1999 -O2 -fsigned-char -ffp-contract=off \
                -Wall -Wno-format-truncation -Wno-missing-field-initializers \
                -DNDEBUG -DTARGET_UNIX -DWITH_NETWORK -DTYRIAN_DIR=\"data\"

ifneq ($(OPENTYRIAN_COMMIT),)
LOCAL_CFLAGS += -DOPENTYRIAN_COMMIT=\"$(OPENTYRIAN_COMMIT)\"
endif

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_net

LOCAL_LDLIBS := -lm -llog

include $(BUILD_SHARED_LIBRARY)
