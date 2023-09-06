
LOCAL_PATH := $(call my-dir)/../../deps/bzip2


include $(CLEAR_VARS)

LOCAL_MODULE    := lzbzip2

LOCAL_C_INCLUDES :=   $(LOCAL_PATH)/include/

LOCAL_SRC_FILES =  	\
	  blocksort.c \
    bzlib.c \
    compress.c \
    crctable.c \
    decompress.c \
    huffman.c \
    randtable.c \

LOCAL_CFLAGS += -fvisibility=hidden -fdata-sections -ffunction-sections

include $(BUILD_STATIC_LIBRARY)








