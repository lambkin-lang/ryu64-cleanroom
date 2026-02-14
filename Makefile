CC ?= cc
AR ?= ar
RM ?= rm -f
WASM_CC ?= clang

CFLAGS_BASE ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
WASM_CFLAGS_BASE ?= -std=c11 -Oz -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude
LDFLAGS_BASE ?=

SRC_FMT_TINY = \
	src/ryu64_bigint.c \
	src/ryu64_shortest.c \
	src/ryu64_9sig.c \
	src/ryu64_tables_shortest.c

SRC_FMT_FULL = \
	$(SRC_FMT_TINY) \
	src/ryu64_printf.c \
	src/ryu64_tables_printf.c

SRC_PARSE = \
	src/ryu64_parse_tiny.c \
	src/ryu64_parse_full.c \
	src/ryu64_parse_tables.c

SRC_TINY = $(SRC_FMT_TINY) $(SRC_PARSE)
SRC_FULL = $(SRC_FMT_FULL) $(SRC_PARSE)
SRC_TEST = $(SRC_FULL)

OBJ_TINY = $(SRC_TINY:src/%.c=build/tiny/%.o)
OBJ_FULL = $(SRC_FULL:src/%.c=build/full/%.o)
OBJ_TEST = $(SRC_TEST:src/%.c=build/test/%.o)
OBJ_WASM_TINY = $(SRC_TINY:src/%.c=build/wasm-tiny/%.o)

.PHONY: all tiny full test wasm-tiny clean

all: full

tiny: build/libryu64_tiny.a

full: build/libryu64_full.a

test: build/test_ryu64
	./build/test_ryu64

wasm-tiny: $(OBJ_WASM_TINY)

build/libryu64_tiny.a: $(OBJ_TINY)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_TINY)

build/libryu64_full.a: $(OBJ_FULL)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_FULL)

build/test_ryu64: $(OBJ_TEST) build/test/test_ryu64.o
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) $(LDFLAGS_BASE) -o $@ $(OBJ_TEST) build/test/test_ryu64.o

build/tiny/%.o: src/%.c include/ryu64.h src/ryu64_internal.h src/ryu64_parse_internal.h
	@mkdir -p build/tiny
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_TINY -c $< -o $@

build/full/%.o: src/%.c include/ryu64.h src/ryu64_internal.h src/ryu64_parse_internal.h
	@mkdir -p build/full
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/%.o: src/%.c include/ryu64.h src/ryu64_internal.h src/ryu64_parse_internal.h
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -DRYU_TIER_TEST -DRYU_ENABLE_LIBC_ORACLE -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/test_ryu64.o: test/test_ryu64.c include/ryu64.h
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) -DRYU_TIER_FULL -DRYU_TIER_TEST -DRYU_ENABLE_LIBC_ORACLE -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/wasm-tiny/%.o: src/%.c include/ryu64.h src/ryu64_internal.h src/ryu64_parse_internal.h
	@mkdir -p build/wasm-tiny
	$(WASM_CC) --target=wasm32 $(WASM_CFLAGS_BASE) -DRYU_TIER_TINY -c $< -o $@

clean:
	$(RM) -r build
