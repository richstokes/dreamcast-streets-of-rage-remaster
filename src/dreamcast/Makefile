ROOT := ../..
TARGET := $(ROOT)/build/native/sor.elf
KOS_BUILD_SUBARCHS = pristine
include $(KOS_BASE)/Makefile.rules
SOURCES := $(wildcard $(ROOT)/build/native/upstream/*.cpp) $(wildcard *.cpp) ../render/vdp_scene.cpp ../audio/audio_core.cpp ../audio/dac_driver.cpp
OBJS := $(SOURCES:.cpp=.o) $(ROOT)/build/native/memory.o $(ROOT)/build/native/save.o
# Keep the synthesizer and its callbacks visible to the optimizer across files.
# No fast-math: the PCM regression must remain byte-for-byte identical.
# Repeat -O3 on the link command: KOS supplies a later -O2 in KOS_LDFLAGS,
# which otherwise downgrades LTO optimization of these audio objects.
AUDIO_OBJS := ../audio/audio_core.o ../audio/dac_driver.o $(wildcard $(ROOT)/build/native/upstream/ymfm_*.cpp)
AUDIO_OBJS := $(AUDIO_OBJS:.cpp=.o)
$(AUDIO_OBJS): KOS_CPPFLAGS += -O3 -flto -DNDEBUG
# VDP port traffic (status polls, data-port uploads) calls small VDPState
# accessors from another file on every access; let LTO inline them.
VDP_OBJS := $(ROOT)/build/native/upstream/VDPState.o $(ROOT)/build/native/upstream/VDPPort.o runtime.o
$(VDP_OBJS): KOS_CPPFLAGS += -O3 -flto
KOS_CPPFLAGS += -std=gnu++23 -g -I. -Icompat -I../render -I../audio -I$(ROOT)/include -I$(ROOT)/build/native/upstream -ffunction-sections -fdata-sections -MMD -MP
KOS_CFLAGS += -g -I$(ROOT)/include -ffunction-sections -fdata-sections -MMD -MP
KOS_LDFLAGS += -Wl,--gc-sections,-Map,$(ROOT)/build/native/sor.map
all: $(TARGET)
$(OBJS): Makefile
-include $(OBJS:.o=.d)
$(TARGET): $(OBJS)
	kos-c++ -O3 -flto -o $@ $(OBJS) -lm
$(ROOT)/build/native/sor-test.elf: $(OBJS) $(ROOT)/build/native/embedded-rom.o
	kos-c++ -O3 -flto -o $@ $(OBJS) $(ROOT)/build/native/embedded-rom.o -lm
$(ROOT)/build/native/memory.o: $(ROOT)/src/core/memory.c
	kos-cc -c $< -o $@
$(ROOT)/build/native/save.o: $(ROOT)/src/core/save.c
	kos-cc -c $< -o $@
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean
