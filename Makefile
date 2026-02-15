CC ?= cc
AR ?= ar
RM ?= rm -f
WASM_CC ?= clang

WASI_SDK ?= /Users/mshonle/Projects/lambkin-lang/build-tools/wasi-sdk
WASI_SYSROOT ?= $(WASI_SDK)/share/wasi-sysroot
WASI_CC_CMD = $(WASI_SDK)/bin/clang --target=wasm32-wasi --sysroot=$(WASI_SYSROOT)
WASI_CFLAGS = -std=c11 -Os -DNDEBUG -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-Iwasm_compat -Iinclude -Iwasi
WASI_BUILTINS = $(shell $(WASI_SDK)/bin/clang --target=wasm32-wasi -print-libgcc-file-name 2>/dev/null)
WASI_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
WASMTIME ?= wasmtime
WASM_OPT ?= /opt/homebrew/bin/wasm-opt

UNAME_S := $(shell uname -s)

CFLAGS_BASE ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
WASM_CFLAGS_BASE ?= -std=c11 -Oz -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude
LDFLAGS_BASE ?=

SPEED_CFLAGS ?= -std=c11 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude
SIZE_CFLAGS ?= -std=c11 -Oz -DNDEBUG -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -Iinclude
ifeq ($(UNAME_S),Darwin)
  SIZE_LDFLAGS ?= -Wl,-dead_strip
else
  SIZE_LDFLAGS ?= -Wl,--gc-sections
endif

LIBS_MATH = -lm
DEPFLAGS = -MMD -MP -MT $@ -MF $(@:.o=.d)

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
SRC_ORACLE = $(SRC_FULL)

OBJ_TINY = $(SRC_TINY:src/%.c=build/tiny/%.o)
OBJ_FULL = $(SRC_FULL:src/%.c=build/full/%.o)
OBJ_TEST = $(SRC_TEST:src/%.c=build/test/%.o)
OBJ_WASM_TINY = $(SRC_TINY:src/%.c=build/wasm-tiny/%.o)
OBJ_ORACLE_SPEED = $(SRC_ORACLE:src/%.c=build/oracle-speed/%.o) build/oracle-speed/oracle_stdio.o
OBJ_ORACLE_SIZE = $(SRC_ORACLE:src/%.c=build/oracle-size/%.o) build/oracle-size/oracle_stdio.o
OBJ_NOLIBC_SPEED = $(SRC_FULL:src/%.c=build/nolibc-speed/%.o) build/nolibc-speed/test_ryu64.o
OBJ_NOLIBC_SIZE = $(SRC_FULL:src/%.c=build/nolibc-size/%.o) build/nolibc-size/test_ryu64.o
OBJ_WASM_COMPAT_SPEED = build/nolibc-speed/wasm_compat_string.o
OBJ_WASM_COMPAT_SIZE = build/nolibc-size/wasm_compat_string.o
OBJ_WASI_RUN = $(SRC_FULL:src/%.c=build/wasi-run/%.o) \
	build/wasi-run/wasm_compat_string.o \
	build/wasi-run/wasi_start.o \
	build/wasi-run/smoke_main.o
DEPS = \
	$(OBJ_TINY:.o=.d) \
	$(OBJ_FULL:.o=.d) \
	$(OBJ_TEST:.o=.d) \
	$(OBJ_WASM_TINY:.o=.d) \
	$(OBJ_ORACLE_SPEED:.o=.d) \
	$(OBJ_ORACLE_SIZE:.o=.d) \
	$(OBJ_NOLIBC_SPEED:.o=.d) \
	$(OBJ_NOLIBC_SIZE:.o=.d) \
	$(OBJ_WASM_COMPAT_SPEED:.o=.d) \
	$(OBJ_WASM_COMPAT_SIZE:.o=.d) \
	$(OBJ_WASI_RUN:.o=.d) \
	build/test/test_ryu64.d

.PHONY: all tiny full test oracle-test benchmark-speed benchmark-size wasm-tiny nolibc-check-speed nolibc-check-size wasm-run shootout gen-parse-pow10 clean

all: full

tiny: build/libryu64_tiny.a

full: build/libryu64_full.a

test: build/test_ryu64 build/oracle_ryu64_speed
	./build/test_ryu64
	./build/oracle_ryu64_speed --quick

oracle-test: build/oracle_ryu64_speed
	./build/oracle_ryu64_speed --quick

benchmark-speed: build/oracle_ryu64_speed
	./build/oracle_ryu64_speed

benchmark-size: build/oracle_ryu64_size
	./build/oracle_ryu64_size

wasm-tiny: $(OBJ_WASM_TINY)

nolibc-check-speed: $(OBJ_NOLIBC_SPEED) $(OBJ_WASM_COMPAT_SPEED)

nolibc-check-size: $(OBJ_NOLIBC_SIZE) $(OBJ_WASM_COMPAT_SIZE)

wasm-run: build/ryu64_smoke.wasm
	$(WASMTIME) run $<

gen-parse-pow10: build/gen_pow10_u128
	./build/gen_pow10_u128

build/libryu64_tiny.a: $(OBJ_TINY)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_TINY)

build/libryu64_full.a: $(OBJ_FULL)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ_FULL)

build/test_ryu64: $(OBJ_TEST) build/test/test_ryu64.o
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) $(LDFLAGS_BASE) -o $@ $(OBJ_TEST) build/test/test_ryu64.o $(LIBS_MATH)

build/oracle_ryu64_speed: $(OBJ_ORACLE_SPEED)
	@mkdir -p build
	$(CC) $(SPEED_CFLAGS) $(LDFLAGS_BASE) -o $@ $(OBJ_ORACLE_SPEED) $(LIBS_MATH)

build/oracle_ryu64_size: $(OBJ_ORACLE_SIZE)
	@mkdir -p build
	$(CC) $(SIZE_CFLAGS) $(LDFLAGS_BASE) $(SIZE_LDFLAGS) -o $@ $(OBJ_ORACLE_SIZE) $(LIBS_MATH)

build/tiny/%.o: src/%.c
	@mkdir -p build/tiny
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU_TIER_TINY -c $< -o $@

build/full/%.o: src/%.c
	@mkdir -p build/full
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/%.o: src/%.c
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU_TIER_TEST -DRYU_ENABLE_LIBC_ORACLE -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU_TIER_TEST -DRYU_ENABLE_LIBC_ORACLE -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/wasm-tiny/%.o: src/%.c
	@mkdir -p build/wasm-tiny
	$(WASM_CC) --target=wasm32 $(WASM_CFLAGS_BASE) $(DEPFLAGS) -DRYU_TIER_TINY -c $< -o $@

build/oracle-speed/%.o: src/%.c
	@mkdir -p build/oracle-speed
	$(CC) $(SPEED_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_ENABLE_LIBC_ORACLE -c $< -o $@

build/oracle-size/%.o: src/%.c
	@mkdir -p build/oracle-size
	$(CC) $(SIZE_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_ENABLE_LIBC_ORACLE -c $< -o $@

build/oracle-speed/oracle_stdio.o: test/oracle_stdio.c
	@mkdir -p build/oracle-speed
	$(CC) $(SPEED_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_ENABLE_LIBC_ORACLE -c $< -o $@

build/oracle-size/oracle_stdio.o: test/oracle_stdio.c
	@mkdir -p build/oracle-size
	$(CC) $(SIZE_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_ENABLE_LIBC_ORACLE -c $< -o $@

build/nolibc-speed/%.o: src/%.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) --target=wasm32 -std=c11 -O3 -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/%.o: src/%.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) --target=wasm32 -std=c11 -Oz -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-speed/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) --target=wasm32 -std=c11 -O3 -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-speed/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) --target=wasm32 -std=c11 -O3 -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) --target=wasm32 -std=c11 -Oz -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) --target=wasm32 -std=c11 -Oz -DNDEBUG -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude $(DEPFLAGS) -DRYU_NO_LIBC_TEST -c $< -o $@

build/wasi-run/%.o: src/%.c
	@mkdir -p build/wasi-run
	$(WASI_CC_CMD) $(WASI_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/wasi-run/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/wasi-run
	$(WASI_CC_CMD) $(WASI_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-run/wasi_start.o: wasi/wasi_start.c
	@mkdir -p build/wasi-run
	$(WASI_CC_CMD) $(WASI_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-run/smoke_main.o: wasi/smoke_main.c wasi/wasi_io.h
	@mkdir -p build/wasi-run
	$(WASI_CC_CMD) $(WASI_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/ryu64_smoke.wasm: $(OBJ_WASI_RUN)
	@mkdir -p build
	$(WASI_CC_CMD) $(WASI_CFLAGS) $^ $(WASI_BUILTINS) -o $@ $(WASI_LDFLAGS)

build/gen_pow10_u128: tools/gen_pow10_u128.c
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) -o $@ $<

# --- shootout: ryu64-full vs snprintf size comparison ---

SHOOTOUT_NATIVE_CFLAGS = -std=c11 -Oz -DNDEBUG -flto \
	-ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
SHOOTOUT_WASI_NOLIBC_CFLAGS = -std=c11 -Oz -DNDEBUG -flto -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-fno-unwind-tables -fno-asynchronous-unwind-tables
SHOOTOUT_WASI_LIBC_CFLAGS = -std=c11 -Oz -DNDEBUG -flto \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-fno-unwind-tables -fno-asynchronous-unwind-tables

shootout: build/shootout_ryu64_native build/shootout_ryu64.wasm build/shootout_snprintf_native build/shootout_snprintf.wasm
	@echo ""
	@echo "=== shootout sizes ==="
	@printf "  %-24s %6d bytes\n" "ryu64 native:" "$$(wc -c < build/shootout_ryu64_native | tr -d ' ')"
	@printf "  %-24s %6d bytes\n" "ryu64 wasm:" "$$(wc -c < build/shootout_ryu64.wasm | tr -d ' ')"
	@printf "  %-24s %6d bytes\n" "snprintf native:" "$$(wc -c < build/shootout_snprintf_native | tr -d ' ')"
	@printf "  %-24s %6d bytes\n" "snprintf wasm:" "$$(wc -c < build/shootout_snprintf.wasm | tr -d ' ')"
	@echo ""
	@echo "=== ryu64 native ==="
	@./build/shootout_ryu64_native
	@echo "=== ryu64 wasm ==="
	@$(WASMTIME) run build/shootout_ryu64.wasm
	@echo "=== snprintf native ==="
	@./build/shootout_snprintf_native
	@echo "=== snprintf wasm ==="
	@$(WASMTIME) run build/shootout_snprintf.wasm

build/shootout_ryu64_native: $(SRC_FMT_FULL) shootout/ryu64_print.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -DRYU_TIER_FULL -Iinclude -Ishootout \
		$(SRC_FMT_FULL) shootout/ryu64_print.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_ryu64_raw.wasm: $(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_NOLIBC_CFLAGS) -DRYU_TIER_FULL -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		-nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_ryu64.wasm: build/shootout_ryu64_raw.wasm
	$(WASM_OPT) -Oz $< -o $@

build/shootout_snprintf_native: shootout/snprintf_print.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -Ishootout \
		shootout/snprintf_print.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_snprintf_raw.wasm: shootout/snprintf_print.c shootout/values.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_CFLAGS) -Ishootout \
		shootout/snprintf_print.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_snprintf.wasm: build/shootout_snprintf_raw.wasm
	$(WASM_OPT) -Oz $< -o $@

clean:
	$(RM) -r build

-include $(DEPS)
