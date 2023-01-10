################################################################################

CC=gcc # define the compiler to use
LD=ld # define the linker to use
SRCS := \
			src/common/strsub.c              \
			src/parts/toaster.c              \
			src/parts/debug.c                \
			src/parts/console.c              \
			src/parts/engine.c               \
			src/parts/parts.c                \
			src/parts/scommon.c              \
			src/tool/lex.c                   \
			src/tool/machine.c               \
			src/tool/collection.c            \
			src/tool/parse.c                 \
			src/engine.c                     \
			src/port/engine_posix.c          \
			src/starter.c                    \
			test/main.c
CFLAGS=-Os
LDFLAGS=-lpthread --static -Xlinker -Map=output.map -T engine.ld

TARGET_EXEC ?= engine

BUILD_DIR ?= ./build
SRC_DIRS ?= ./Sources

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o) 
DEPS := $(OBJS:.o=.d)
LDS := 

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) $(LDS) -o $@ $(LDFLAGS)

# assembly
$(BUILD_DIR)/%.s.o: %.s
	$(MKDIR_P) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@


.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= mkdir -p
