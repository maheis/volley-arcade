CC := gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
ARCH ?=
ARCH_FLAGS :=
ifeq ($(ARCH),32)
ARCH_FLAGS := -m32
endif

SDL_CONFIG ?= sdl2-config
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags 2>/dev/null)
SDL_LIBS := $(shell $(SDL_CONFIG) --libs 2>/dev/null)

ifeq ($(strip $(SDL_CFLAGS)),)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
endif

ifeq ($(strip $(SDL_CFLAGS)),)
$(error SDL2 development files not found. Install libsdl2-dev)
endif

TARGET := volley-arcade
SRC := src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(ARCH_FLAGS) $(SDL_CFLAGS) $(SRC) -o $@ $(ARCH_FLAGS) $(SDL_LIBS) -lm

build32:
	$(MAKE) ARCH=32 all

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all build32 run clean
