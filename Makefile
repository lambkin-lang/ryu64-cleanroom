CC ?= cc
AR ?= ar
RM ?= rm -f

CFLAGS_BASE ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS_BASE ?=

SRC_COMMON = \
	src/ryu64_bigint.c \
	src/ryu64_shortest.c \
	src/ryu64_9sig.c \
	src/ryu64_printf.c \
	src/ryu64_tables_shortest.c \
	src/ryu64_tables_printf.c

OBJ_TINY = $(SRC_COMMON:src/%.c=build/tiny/%.o)
OBJ_FULL = $(SRC_COMMON:src/%.c=build/full/%.o)
OBJ_TEST = $(SRC_COMMON:src/%.c=build/test/%.o)

.PHONY: all tiny full test clean

all: full

tiny: build/libryu64_tiny.a

full: build/libryu64_full.a

test: build/test_ryu64
	./build/test_ryu64

build/libryu64_tiny.a: $(OBJ_TINY)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_TINY)

build/libryu64_full.a: $(OBJ_FULL)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_FULL)

build/test_ryu64: $(OBJ_TEST) build/test/test_ryu64.o
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) $(LDFLAGS_BASE) -o $@ $(OBJ_TEST) build/test/test_ryu64.o

build/tiny/%.o: src/%.c include/ryu64.h src/ryu64_internal.h
	@mkdir -p build/tiny
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_TINY -c $< -o $@

build/full/%.o: src/%.c include/ryu64.h src/ryu64_internal.h
	@mkdir -p build/full
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -c $< -o $@

build/test/%.o: src/%.c include/ryu64.h src/ryu64_internal.h
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -DRYU_TIER_TEST -c $< -o $@

build/test/test_ryu64.o: test/test_ryu64.c include/ryu64.h
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -DRYU_TIER_TEST -c $< -o $@

clean:
	$(RM) -r build
