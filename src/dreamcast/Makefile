ROOT := ../..
TARGET := $(ROOT)/build/native/sor.elf
KOS_BUILD_SUBARCHS = pristine
include $(KOS_BASE)/Makefile.rules
SOURCES := $(wildcard $(ROOT)/build/native/upstream/*.cpp) $(wildcard *.cpp)
OBJS := $(SOURCES:.cpp=.o) $(ROOT)/build/native/memory.o $(ROOT)/build/native/save.o
KOS_CPPFLAGS += -std=gnu++23 -g -Icompat -I$(ROOT)/include -I$(ROOT)/build/native/upstream -ffunction-sections -fdata-sections
KOS_CFLAGS += -std=c11 -g -I$(ROOT)/include -ffunction-sections -fdata-sections
KOS_LDFLAGS += -Wl,--gc-sections,-Map,$(ROOT)/build/native/sor.map
all: $(TARGET)
$(TARGET): $(OBJS)
	kos-c++ $(KOS_LDFLAGS) -o $@ $(OBJS) -lm
$(ROOT)/build/native/memory.o: $(ROOT)/src/core/memory.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
$(ROOT)/build/native/save.o: $(ROOT)/src/core/save.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@
clean:
	rm -f $(OBJS) $(TARGET)
.PHONY: all clean
