LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := pktriggercord-cli
LOCAL_SRC_FILES := ../../src/external/js0n/js0n.c \
	../../pslr_enum.c \
	../../pslr_lens.c \
	../../pslr_model.c \
	../../pslr_scsi.c \
	../../pslr.c \
	../../pktriggercord-servermode.c \
	../../pktriggercord-cli.c
DEFINES 	:= -DANDROID -DVERSION=\"$(VERSION)\" -DPKTDATADIR=\".\"
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../src/external/js0n
LOCAL_CFLAGS  	:= $(DEFINES) -frtti -Istlport
LOCAL_LDLIBS	:= -llog -lstdc++

include $(BUILD_EXECUTABLE)
