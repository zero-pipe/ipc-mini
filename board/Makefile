# Board build for zero-mini (Hi3516CV610).
#
#   cd /any/path/zero-mini
#   source /etc/profile && make -f board/Makefile -j$(nproc)
#
# Optional KVS WebRTC media stack (after you cross-build the SDK under 3rdpart):
#   make -f board/Makefile ZERO_MINI_ENABLE_KVS=1 -j$(nproc)
#   # default KVS_ROOT=$(ROOT)/3rdpart/kvs-webrtc-sdk

include board/Makefile.param

ROOT := .
DEVICE := $(ROOT)/devices/hisi_3516cv610
ZERO_MINI_ENABLE_KVS ?= 0
KVS_ROOT ?= $(ROOT)/3rdpart/kvs-webrtc-sdk

CXX := $(CROSS)g++
CC  := $(CROSS)gcc

CXXFLAGS += -std=c++17 -Wno-reorder -Wno-psabi
CFLAGS   += -static -ffunction-sections -fdata-sections
CFLAGS   += -include stddef.h
CFLAGS   += -DNPU -DSENSOR0_TYPE=$(SENSOR0_TYPE) -DSENSOR1_TYPE=$(SENSOR1_TYPE)
CFLAGS   += -DZERO_MINI_ENABLE_KVS=$(ZERO_MINI_ENABLE_KVS)

INC_PATH += -I$(ROOT)/include
INC_PATH += -I$(ROOT)/device_adapter/include
INC_PATH += -I$(ROOT)/protocol/webrtc
INC_PATH += -I$(ROOT)/board
INC_PATH += -I$(ROOT)/3rdpart/jsoncpp
INC_PATH += -I$(ROOT)/3rdpart/freetype-2.7.1/include/freetype2
INC_PATH += -I$(ROOT)/3rdpart/zero-tool-kit/include
INC_PATH += -I$(DEVICE)
INC_PATH += -I$(DEVICE)/support
INC_PATH += -I$(DEVICE)/sensor/common

ifeq ($(ZERO_MINI_ENABLE_KVS),1)
KVS_BUILD ?= $(KVS_ROOT)/build-musl
KVS_OPEN  ?= $(KVS_ROOT)/open-source
INC_PATH += -I$(KVS_ROOT)/src/include
INC_PATH += -I$(KVS_OPEN)/include
endif

BUILD := build
target := zero_mini

LOCAL_CXX := \
	app/main.cpp \
	src/config/runtime_config.cpp \
	src/media/media_frame.cpp \
	src/media/gop_cache.cpp \
	src/media/media_source.cpp \
	src/media/detection_hub.cpp \
	src/core/protocol_poller.cpp \
	src/core/application.cpp \
	protocol/webrtc/ws_client.cpp \
	protocol/webrtc/signaling_client.cpp \
	protocol/webrtc/signaling_message_codec.cpp \
	protocol/webrtc/kvs_peer_session.cpp \
	protocol/webrtc/webrtc_plugin.cpp \
	device_adapter/src/encoded_frame_adapter.cpp \
	device_adapter/src/ai_stream_bridge.cpp \
	device_adapter/src/encoded_stream_channel.cpp \
	device_adapter/src/hisilicon_pipeline.cpp \
	3rdpart/jsoncpp/json_reader.cpp \
	3rdpart/jsoncpp/json_value.cpp \
	3rdpart/jsoncpp/json_writer.cpp

ZTK := 3rdpart/zero-tool-kit/src
LOCAL_C := \
	$(ZTK)/core/ztk_errno.c \
	$(ZTK)/core/ztk.c \
	$(ZTK)/platform/platform.c \
	$(ZTK)/platform/linux/platform_linux.c \
	$(ZTK)/platform/linux/sync_pthread.c \
	$(ZTK)/platform/linux/sem_pthread.c \
	$(ZTK)/platform/linux/thread_pthread.c \
	$(ZTK)/platform/linux/thread_priority_linux.c \
	$(ZTK)/util/log.c \
	$(ZTK)/util/byte_buf.c \
	$(ZTK)/util/buf.c \
	$(ZTK)/util/mpsc.c \
	$(ZTK)/util/timer.c \
	$(ZTK)/net/socket.c \
	$(ZTK)/net/tcp_server.c \
	$(ZTK)/net/tcp_client.c \
	$(ZTK)/net/udp_server.c \
	$(ZTK)/net/udp_client.c \
	$(ZTK)/net/rtp_port.c \
	$(ZTK)/net/linux/sock_linux.c \
	$(ZTK)/poller/poller_task.c \
	$(ZTK)/poller/poller_timer.c \
	$(ZTK)/poller/poller_load.c \
	$(ZTK)/poller/poller_pool.c \
	$(ZTK)/poller/poller_buf.c \
	$(ZTK)/poller/poller_epoll.c \
	$(ZTK)/thread/thread_pool.c

DEVICE_CXX := \
	dev_sys.cpp \
	dev_vi.cpp \
	dev_vi_isp.cpp \
	dev_vi_sc4336p_liner.cpp \
	dev_vi_hy006_3814_0011_liner.cpp \
	dev_vi_sc431hai_liner.cpp \
	dev_vi_gc8613_liner.cpp \
	dev_vi_gc4023_liner.cpp \
	dev_venc.cpp \
	dev_aenc.cpp \
	dev_aplay.cpp \
	dev_chn.cpp \
	dev_osd.cpp \
	dev_svp.cpp \
	dev_svp_yolov8.cpp \
	dev_std.cpp \
	font_renderer.cpp

DEVICE_C := \
	sensor/common/sensor_common.c \
	sensor/smart_sc4336p/sc4336p_cmos.c \
	sensor/smart_sc4336p/sc4336p_sensor_ctrl.c \
	sensor/galaxycore_gc4023/gc4023_cmos.c \
	sensor/galaxycore_gc4023/gc4023_sensor_ctrl.c \
	sensor/smart_sc431hai/sc431hai_cmos.c \
	sensor/smart_sc431hai/sc431hai_sensor_ctrl.c \
	sensor/hy006_3814_0011/hy006_3814_0011_cmos.c \
	sensor/hy006_3814_0011/hy006_3814_0011_sensor_ctrl.c \
	sensor/galaxycore_gc8613/1080p20/gc8613_cmos.c \
	sensor/galaxycore_gc8613/1080p20/gc8613_sensor_ctrl.c \
	sensor/galaxycore_gc8613/4k20/gc8613_cmos.c \
	sensor/galaxycore_gc8613/4k20/gc8613_sensor_ctrl.c

LIBS += -Wl,--start-group
LIBS += $(ROOT)/3rdpart/freetype-2.7.1/lib/libfreetype.a
LIBS += $(REL_LIB)/libss_mpi.a
LIBS += $(REL_LIB)/libss_mpi_sysbind.a
LIBS += $(REL_LIB)/libss_mpi_sysmem.a
LIBS += $(REL_LIB)/libss_mpi_ae.a
LIBS += $(REL_LIB)/libss_mpi_isp.a
LIBS += $(REL_LIB)/libot_mpi_isp.a
LIBS += $(REL_LIB)/libss_mpi_awb.a
LIBS += $(REL_LIB)/libdehaze.a
LIBS += $(REL_LIB)/libextend_stats.a
LIBS += $(REL_LIB)/libdrc.a
LIBS += $(REL_LIB)/libldci.a
LIBS += $(REL_LIB)/libbnr.a
LIBS += $(REL_LIB)/libcalcflicker.a
LIBS += $(REL_LIB)/libacs.a
LIBS += $(REL_LIB)/libir_auto.a
LIBS += $(REL_LIB)/libss_mpi_aibnr.a
LIBS += $(REL_LIB)/libsvp_acl.a
LIBS += $(REL_LIB)/libprotobuf-c.a
LIBS += $(REL_LIB)/libss_mpi_aidetect.a
LIBS += $(REL_LIB)/libaiservice_comm.a
LIBS += $(REL_LIB)/libss_mpi_cipher.a
LIBS += $(REL_LIB)/libot_osal.a
LIBS += $(REL_LIB)/libss_mpi_audio.a
LIBS += $(REL_LIB)/libss_mpi_audio_adp.a
LIBS += $(REL_LIB)/libvoice_engine.a
LIBS += $(REL_LIB)/libaac_enc.a
LIBS += $(REL_LIB)/libaac_dec.a
LIBS += $(REL_LIB)/libaac_comm.a
LIBS += $(REL_LIB)/libdnvqe.a
LIBS += $(REL_LIB)/libaac_sbr_enc.a
LIBS += $(REL_LIB)/libaac_sbr_dec.a
LIBS += $(REL_LIB)/libupvqe.a
LIBS += $(REL_LIB)/libsecurec.a
ifeq ($(ZERO_MINI_ENABLE_KVS),1)
# PeerConnection media path only. Do NOT link AWS SignalingClient /
# kvsCommonLws / libwebsockets — zero-mini uses its own WS signaling.
LIBS += $(KVS_BUILD)/libkvsWebrtcClient.a
LIBS += $(KVS_OPEN)/lib/libkvspic.a
LIBS += $(KVS_OPEN)/lib/libkvspicState.a
LIBS += $(KVS_OPEN)/lib/libkvspicUtils.a
LIBS += $(KVS_OPEN)/lib/libsrtp2.a
LIBS += $(KVS_OPEN)/lib/libusrsctp.a
LIBS += $(KVS_OPEN)/lib/libmbedtls.a
LIBS += $(KVS_OPEN)/lib/libmbedx509.a
LIBS += $(KVS_OPEN)/lib/libmbedcrypto.a
endif
LIBS += -Wl,--end-group
LIBS += -Wl,-gc-sections
LIBS += -Wl,-Map,zero_mini.map
LIBS += -lpthread -ldl -lrt -lm

LOCAL_CXX_OBJ := $(addprefix $(BUILD)/local/,$(LOCAL_CXX:.cpp=.o))
LOCAL_C_OBJ   := $(addprefix $(BUILD)/local/,$(LOCAL_C:.c=.o))
DEVICE_CXX_OBJ := $(addprefix $(BUILD)/device/,$(DEVICE_CXX:.cpp=.o))
DEVICE_C_OBJ   := $(addprefix $(BUILD)/device/,$(DEVICE_C:.c=.o))

ALL_OBJ := $(LOCAL_CXX_OBJ) $(LOCAL_C_OBJ) $(DEVICE_CXX_OBJ) $(DEVICE_C_OBJ)
ALL_DEP := $(ALL_OBJ:.o=.d)

.PHONY: all clean check

all: check $(target)

check:
	@test -f "$(DEVICE)/dev_chn.cpp" || (echo "missing device tree: $(DEVICE)"; exit 1)
	@test -f "app/main.cpp" || (echo "run make from zero-mini root"; exit 1)

$(target): $(ALL_OBJ)
	@echo "LD $(target)"
	@$(CXX) $^ -o $@ $(CFLAGS) $(LIBS)
	@ls -lh $@
	@file $@

$(BUILD)/local/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(CXX) -MMD -MP -c -o $@ $< $(INC_PATH) $(CFLAGS) $(CXXFLAGS)

$(BUILD)/local/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	@$(CC) -MMD -MP -c -o $@ $< $(INC_PATH) $(CFLAGS)

$(BUILD)/device/%.o: $(DEVICE)/%.cpp
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(CXX) -MMD -MP -c -o $@ $< $(INC_PATH) $(CFLAGS) $(CXXFLAGS)

$(BUILD)/device/%.o: $(DEVICE)/%.c
	@mkdir -p $(dir $@)
	@echo "CC  $<"
	@$(CC) -MMD -MP -c -o $@ $< $(INC_PATH) $(CFLAGS)

-include $(ALL_DEP)

clean:
	-rm -rf $(BUILD) $(target) zero_mini.map
