ROOT := ../..
TARGET := $(ROOT)/build/native/sor.elf
KOS_BUILD_SUBARCHS = pristine
include $(KOS_BASE)/Makefile.rules
SOURCES := $(wildcard $(ROOT)/build/native/upstream/*.cpp) $(wildcard *.cpp) ../render/vdp_scene.cpp
OBJS := $(SOURCES:.cpp=.o) $(ROOT)/build/native/memory.o $(ROOT)/build/native/save.o
KOS_CPPFLAGS += -std=gnu++23 -g -Icompat -I../render -I$(ROOT)/include -I$(ROOT)/build/native/upstream -ffunction-sections -fdata-sections -MMD -MP
KOS_CFLAGS += -g -I$(ROOT)/include -ffunction-sections -fdata-sections -MMD -MP
KOS_LDFLAGS += -Wl,--gc-sections,-Map,$(ROOT)/build/native/sor.map
all: $(TARGET)
$(OBJS): Makefile
-include $(OBJS:.o=.d)
$(TARGET): $(OBJS)
	kos-c++ -o $@ $(OBJS) -lm
$(ROOT)/build/native/sor-test.elf: $(OBJS) $(ROOT)/build/native/embedded-rom.o
	kos-c++ -o $@ $(OBJS) $(ROOT)/build/native/embedded-rom.o -lm
$(ROOT)/build/native/memory.o: $(ROOT)/src/core/memory.c
	kos-cc -c $< -o $@
$(ROOT)/build/native/save.o: $(ROOT)/src/core/save.c
	kos-cc -c $< -o $@
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean
