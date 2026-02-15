CC ?= cc
AR ?= ar
RM ?= rm -f
WASM_CC ?= clang

WASI_SDK ?= /Users/mshonle/Projects/lambkin-lang/build-tools/wasi-sdk
WASI_SYSROOT ?= $(WASI_SDK)/share/wasi-sysroot
WASI_CC_CMD = $(WASI_SDK)/bin/clang --target=wasm32-wasi --sysroot=$(WASI_SYSROOT)
WASI_OPT_LEVEL ?= z
WASI_ENABLE_LTO ?= 1
WASI_MVP_FEATURE_FLAGS = \
	-Xclang -target-feature -Xclang -mutable-globals \
	-Xclang -target-feature -Xclang -sign-ext \
	-Xclang -target-feature -Xclang -reference-types \
	-Xclang -target-feature -Xclang -multivalue \
	-Xclang -target-feature -Xclang -bulk-memory
WASI_GCPLUS_FEATURE_FLAGS = \
	-Xclang -target-feature -Xclang +mutable-globals \
	-Xclang -target-feature -Xclang +sign-ext \
	-Xclang -target-feature -Xclang +reference-types \
	-Xclang -target-feature -Xclang +multivalue \
	-Xclang -target-feature -Xclang +bulk-memory
WASI_CFLAGS = -std=c11 -O$(WASI_OPT_LEVEL) -DNDEBUG -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-Iwasm_compat -Iinclude -Iwasi
WASI_BUILTINS = $(shell $(WASI_SDK)/bin/clang --target=wasm32-wasi -print-libgcc-file-name 2>/dev/null)
WASI_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
WASI_MVP_CFLAGS = -std=c11 -Oz -DNDEBUG -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-Iwasm_compat -Iinclude -Iwasi $(WASI_MVP_FEATURE_FLAGS)
WASI_MVP_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
WASI_GCPLUS_CFLAGS = $(WASI_CFLAGS) $(WASI_GCPLUS_FEATURE_FLAGS)
WASI_GCPLUS_LDFLAGS = $(WASI_LDFLAGS)
WASMTIME ?= wasmtime
WASM_OPT ?= /opt/homebrew/bin/wasm-opt
WASM_OPT_HAS_STRIP_DWARF := $(shell $(WASM_OPT) --help 2>/dev/null | grep -q -- '--strip-dwarf' && printf '%s' '--strip-dwarf')
WASM_OPT_HAS_STRIP_PRODUCERS := $(shell $(WASM_OPT) --help 2>/dev/null | grep -q -- '--strip-producers' && printf '%s' '--strip-producers')
WASM_OPT_HAS_VACUUM := $(shell $(WASM_OPT) --help 2>/dev/null | grep -q -- '--vacuum' && printf '%s' '--vacuum')
WASM_OPT_POST_FLAGS = -Oz $(WASM_OPT_HAS_STRIP_DWARF) $(WASM_OPT_HAS_STRIP_PRODUCERS) $(WASM_OPT_HAS_VACUUM)

UNAME_S := $(shell uname -s)

CFLAGS_BASE ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
WASM_CFLAGS_BASE ?= -std=c11 -Oz -Wall -Wextra -Wpedantic -ffreestanding -fno-builtin -nostdinc -Iwasm_compat -Iinclude
LDFLAGS_BASE ?=

SPEED_CFLAGS ?= -std=c11 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude
SIZE_CFLAGS ?= -std=c11 -Oz -DNDEBUG -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -Iinclude
ifeq ($(UNAME_S),Darwin)
  SIZE_LDFLAGS ?= -Wl,-dead_strip
  LIBS_MATH ?=
else
  SIZE_LDFLAGS ?= -Wl,--gc-sections
  LIBS_MATH ?= -lm
endif

ifeq ($(WASI_ENABLE_LTO),1)
  WASI_CFLAGS += -flto
  WASI_LDFLAGS += -flto
endif

LIBS_TEST ?=
LIBS_ORACLE ?= $(LIBS_MATH)
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
OBJ_WASI_MVP = $(SRC_FULL:src/%.c=build/wasi-mvp/%.o) \
	build/wasi-mvp/wasm_compat_string.o \
	build/wasi-mvp/wasi_start.o \
	build/wasi-mvp/smoke_main.o
OBJ_WASI_GCPLUS = $(SRC_FULL:src/%.c=build/wasi-gcplus/%.o) \
	build/wasi-gcplus/wasm_compat_string.o \
	build/wasi-gcplus/wasi_start.o \
	build/wasi-gcplus/smoke_main.o
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
	$(OBJ_WASI_MVP:.o=.d) \
	$(OBJ_WASI_GCPLUS:.o=.d) \
	build/test/test_ryu64.d

.PHONY: all tiny full test oracle-test benchmark-speed benchmark-size wasm-tiny nolibc-check-speed nolibc-check-size wasm-mvp wasm-gcplus wasm-run wasm-run-mvp wasm-run-gcplus wasm-compare shootout shootout-bench shootout-report shootout-report-size shootout-report-perf shootout-report-html shootout-track gen-parse-pow10 clean

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

wasm-mvp: build/ryu64_smoke_mvp.wasm

wasm-gcplus: build/ryu64_smoke_gcplus.wasm

wasm-run: build/ryu64_smoke_gcplus.wasm
	$(WASMTIME) run $<

wasm-run-mvp: build/ryu64_smoke_mvp.wasm
	$(WASMTIME) run $<

wasm-run-gcplus: build/ryu64_smoke_gcplus.wasm
	$(WASMTIME) run $<

wasm-compare: build/ryu64_smoke_mvp.wasm build/ryu64_smoke_gcplus.wasm build/shootout_ryu64_native build/shootout_ryu64_mvp.wasm build/shootout_ryu64_gcplus.wasm shootout-report
	@echo ""
	@echo "=== wasm feature checks ==="
	@echo "mvp target features section:"
	@wasm-objdump -x build/ryu64_smoke_mvp.wasm | sed -n '/name: \"target_features\"/,$$p' | sed -n '1,10p' || true
	@echo "gcplus target features section:"
	@wasm-objdump -x build/ryu64_smoke_gcplus.wasm | sed -n '/name: \"target_features\"/,$$p' | sed -n '1,10p' || true
	@echo "mvp validate:"
	@wasm-tools validate --features=mvp build/ryu64_smoke_mvp.wasm >/dev/null && echo "  OK (MVP-only validation passed)"

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
	$(CC) $(CFLAGS_BASE) $(LDFLAGS_BASE) -o $@ $(OBJ_TEST) build/test/test_ryu64.o $(LIBS_TEST)

build/oracle_ryu64_speed: $(OBJ_ORACLE_SPEED)
	@mkdir -p build
	$(CC) $(SPEED_CFLAGS) $(LDFLAGS_BASE) -o $@ $(OBJ_ORACLE_SPEED) $(LIBS_ORACLE)

build/oracle_ryu64_size: $(OBJ_ORACLE_SIZE)
	@mkdir -p build
	$(CC) $(SIZE_CFLAGS) $(LDFLAGS_BASE) $(SIZE_LDFLAGS) -o $@ $(OBJ_ORACLE_SIZE) $(LIBS_ORACLE)

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

build/wasi-mvp/%.o: src/%.c
	@mkdir -p build/wasi-mvp
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/wasi-mvp/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/wasi-mvp
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-mvp/wasi_start.o: wasi/wasi_start.c
	@mkdir -p build/wasi-mvp
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-mvp/smoke_main.o: wasi/smoke_main.c wasi/wasi_io.h
	@mkdir -p build/wasi-mvp
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-gcplus/%.o: src/%.c
	@mkdir -p build/wasi-gcplus
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $(DEPFLAGS) -DRYU_TIER_FULL -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/wasi-gcplus/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/wasi-gcplus
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-gcplus/wasi_start.o: wasi/wasi_start.c
	@mkdir -p build/wasi-gcplus
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/wasi-gcplus/smoke_main.o: wasi/smoke_main.c wasi/wasi_io.h
	@mkdir -p build/wasi-gcplus
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/ryu64_smoke_mvp_raw.wasm: $(OBJ_WASI_MVP)
	@mkdir -p build
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $^ $(WASI_BUILTINS) -o $@ $(WASI_MVP_LDFLAGS)

build/ryu64_smoke_mvp.wasm: build/ryu64_smoke_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features $< -o $@
	wasm-tools validate --features=mvp $@

build/ryu64_smoke_gcplus_raw.wasm: $(OBJ_WASI_GCPLUS)
	@mkdir -p build
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $^ $(WASI_BUILTINS) -o $@ $(WASI_GCPLUS_LDFLAGS)

build/ryu64_smoke_gcplus.wasm: build/ryu64_smoke_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

build/ryu64_smoke.wasm: build/ryu64_smoke_gcplus.wasm
	cp $< $@

build/gen_pow10_u128: tools/gen_pow10_u128.c
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) -o $@ $<

# --- shootout: ryu64-full vs snprintf size comparison ---

SHOOTOUT_NATIVE_CFLAGS = -std=c11 -O2 -DNDEBUG -flto \
	-ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
SHOOTOUT_NATIVE_ENABLE_POW5_CACHE ?= 1
SHOOTOUT_NATIVE_POW5_STRIDE ?= 16
SHOOTOUT_NATIVE_FLAGS_NOTE = native: -flto -fno-unwind-tables -fno-asynchronous-unwind-tables -DRYU_TIER_FULL
SHOOTOUT_WASM_ENABLE_POW5_CACHE ?= 1
SHOOTOUT_WASM_POW5_STRIDE ?= 16
SHOOTOUT_WASM_POW5_FLAGS =
SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX =
SHOOTOUT_REPORT_DIR ?= build/reports
SHOOTOUT_SIZE_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_size.tsv
SHOOTOUT_PERF_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_perf.tsv
SHOOTOUT_PERF_FAILURE_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_perf_failures.tsv
SHOOTOUT_HTML_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_report.html
SHOOTOUT_HISTORY_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_history.tsv
SHOOTOUT_DEEP_RANDOM ?= 224624
SHOOTOUT_DEEP_SEED ?= 0x9e3779b97f4a7c15
SHOOTOUT_DEEP_WARMUP ?= 1000
SHOOTOUT_DEEP_SAMPLES ?= 8
SHOOTOUT_DEEP_ARGS = --random $(SHOOTOUT_DEEP_RANDOM) --seed $(SHOOTOUT_DEEP_SEED) --warmup $(SHOOTOUT_DEEP_WARMUP) --samples $(SHOOTOUT_DEEP_SAMPLES)
SHOOTOUT_WASI_MVP_CFLAGS = -std=c11 -Oz -DNDEBUG -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-fno-unwind-tables -fno-asynchronous-unwind-tables $(WASI_MVP_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_GCPLUS_CFLAGS = -std=c11 -Oz -DNDEBUG -ffreestanding -fno-builtin -nostdinc \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-fno-unwind-tables -fno-asynchronous-unwind-tables $(WASI_GCPLUS_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_LIBC_CFLAGS = -std=c11 -Oz -DNDEBUG -flto \
	-ffunction-sections -fdata-sections -fno-stack-protector -fvisibility=hidden \
	-fno-unwind-tables -fno-asynchronous-unwind-tables
SHOOTOUT_WASI_LIBC_MVP_CFLAGS = $(SHOOTOUT_WASI_LIBC_CFLAGS) $(WASI_MVP_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_LIBC_GCPLUS_CFLAGS = $(SHOOTOUT_WASI_LIBC_CFLAGS) $(WASI_GCPLUS_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_MVP_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
SHOOTOUT_WASI_GCPLUS_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
ifeq ($(SHOOTOUT_NATIVE_ENABLE_POW5_CACHE),1)
  SHOOTOUT_NATIVE_CFLAGS += -DRYU_ENABLE_POW5_STRIDE_CACHE=1 -DRYU_POW5_STRIDE=$(SHOOTOUT_NATIVE_POW5_STRIDE)
  SHOOTOUT_NATIVE_FLAGS_NOTE += + pow5-stride-cache
endif
ifeq ($(SHOOTOUT_WASM_ENABLE_POW5_CACHE),1)
  SHOOTOUT_WASM_POW5_FLAGS = -DRYU_ENABLE_POW5_STRIDE_CACHE=1 -DRYU_POW5_STRIDE=$(SHOOTOUT_WASM_POW5_STRIDE)
  SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX = + pow5-stride-cache
endif
ifeq ($(WASI_ENABLE_LTO),1)
  SHOOTOUT_WASI_GCPLUS_CFLAGS += -flto
  SHOOTOUT_WASI_GCPLUS_LDFLAGS += -flto
endif

shootout: shootout-report-size build/shootout_ryu64_native build/shootout_ryu64_mvp.wasm build/shootout_ryu64_gcplus.wasm build/shootout_snprintf_native build/shootout_snprintf_mvp.wasm build/shootout_snprintf_gcplus.wasm
	@echo ""
	@echo "=== ryu64 native ==="
	@./build/shootout_ryu64_native
	@echo "=== ryu64 wasm mvp ==="
	@$(WASMTIME) run build/shootout_ryu64_mvp.wasm
	@echo "=== ryu64 wasm gcplus ==="
	@$(WASMTIME) run build/shootout_ryu64_gcplus.wasm
	@echo "=== snprintf native ==="
	@./build/shootout_snprintf_native
	@echo "=== snprintf wasm mvp ==="
	@$(WASMTIME) run build/shootout_snprintf_mvp.wasm
	@echo "=== snprintf wasm gcplus ==="
	@$(WASMTIME) run build/shootout_snprintf_gcplus.wasm

shootout-bench: shootout-report-perf

shootout-report: shootout-report-html shootout-track

shootout-report-size: build/shootout_ryu64_native build/shootout_ryu64_mvp.wasm build/shootout_ryu64_gcplus.wasm build/shootout_snprintf_native build/shootout_snprintf_mvp.wasm build/shootout_snprintf_gcplus.wasm
	@mkdir -p $(SHOOTOUT_REPORT_DIR)
	@{ \
		printf "id\tlabel\tbytes\n"; \
		printf "ryu64_native\tryu64 native\t%s\n" "$$(wc -c < build/shootout_ryu64_native | tr -d ' ')"; \
		printf "ryu64_wasm_mvp\tryu64 wasm mvp\t%s\n" "$$(wc -c < build/shootout_ryu64_mvp.wasm | tr -d ' ')"; \
		printf "ryu64_wasm_gcplus\tryu64 wasm gcplus\t%s\n" "$$(wc -c < build/shootout_ryu64_gcplus.wasm | tr -d ' ')"; \
		printf "snprintf_native\tsnprintf native\t%s\n" "$$(wc -c < build/shootout_snprintf_native | tr -d ' ')"; \
		printf "snprintf_wasm_mvp\tsnprintf wasm mvp\t%s\n" "$$(wc -c < build/shootout_snprintf_mvp.wasm | tr -d ' ')"; \
		printf "snprintf_wasm_gcplus\tsnprintf wasm gcplus\t%s\n" "$$(wc -c < build/shootout_snprintf_gcplus.wasm | tr -d ' ')"; \
	} > $(SHOOTOUT_SIZE_REPORT)
	@echo ""
	@echo "=== shootout sizes ==="
	@tab="$$(printf '\t')"; \
	while IFS="$$tab" read -r id label bytes; do \
		[ "$$id" = "id" ] && continue; \
		printf "  %-24s %8s bytes\n" "$${label}:" "$$bytes"; \
	done < $(SHOOTOUT_SIZE_REPORT)
	@printf "  report file: %s\n" "$(SHOOTOUT_SIZE_REPORT)"

shootout-report-perf: build/shootout_deep_native build/shootout_deep_mvp.wasm build/shootout_deep_gcplus.wasm
	@mkdir -p $(SHOOTOUT_REPORT_DIR)
	@native_out="$$(mktemp)"; \
	mvp_out="$$(mktemp)"; \
	gcplus_out="$$(mktemp)"; \
	tab="$$(printf '\t')"; \
	trap 'rm -f "$$native_out" "$$mvp_out" "$$gcplus_out"' EXIT INT TERM; \
	./build/shootout_deep_native $(SHOOTOUT_DEEP_ARGS) > "$$native_out"; \
	$(WASMTIME) run build/shootout_deep_mvp.wasm -- $(SHOOTOUT_DEEP_ARGS) > "$$mvp_out"; \
	$(WASMTIME) run build/shootout_deep_gcplus.wasm -- $(SHOOTOUT_DEEP_ARGS) > "$$gcplus_out"; \
	extract_corpus() { \
		local file="$$1"; \
		local corpus=""; \
		while IFS="$$tab" read -r tag c1 _; do \
			if [ "$$tag" = "CORPUS" ]; then corpus="$$c1"; break; fi; \
		done < "$$file"; \
		printf "%s" "$$corpus"; \
	}; \
	corpus_native="$$(extract_corpus "$$native_out")"; \
	corpus_mvp="$$(extract_corpus "$$mvp_out")"; \
	corpus_gcplus="$$(extract_corpus "$$gcplus_out")"; \
	if [ -z "$$corpus_native" ] || [ "$$corpus_native" != "$$corpus_mvp" ] || [ "$$corpus_native" != "$$corpus_gcplus" ]; then \
		echo "shootout deep perf error: corpus mismatch across profiles"; \
		exit 1; \
	fi; \
	printf "id\tlabel\tns_per_conv\telapsed_ns\tconversions\tavg_len\tnumeric_fail\tbit_fail\tparse_fail\tformat_fail\tcorpus_size\twarmup\trandom_count\tseed\n" > $(SHOOTOUT_PERF_REPORT); \
	printf "profile\tcandidate\tfailure_type\tinput_bits\tparsed_bits\ttext\n" > $(SHOOTOUT_PERF_FAILURE_REPORT); \
	append_profile() { \
		local profile="$$1"; \
		local file="$$2"; \
		local corpus=""; \
		local warmup=""; \
		local random_count=""; \
		local seed=""; \
		while IFS="$$tab" read -r tag f1 f2 f3 f4 f5 f6 f7 f8 f9 f10; do \
			case "$$tag" in \
				CORPUS) \
					corpus="$$f1"; warmup="$$f2"; random_count="$$f3"; seed="$$f4"; \
					;; \
				RESULT) \
					candidate="$$f1"; conversions="$$f2"; elapsed_ns="$$f3"; ns_per_conv="$$f4"; avg_len="$$f5"; \
					numeric_fail="$$f6"; bit_fail="$$f7"; parse_fail="$$f8"; format_fail="$$f9"; \
					case "$$profile:$$candidate" in \
						native:ryu64) id="ryu64_native"; label="ryu64 native" ;; \
						native:snprintf) id="snprintf_native"; label="snprintf native" ;; \
						wasm_mvp:ryu64) id="ryu64_wasm_mvp"; label="ryu64 wasm mvp" ;; \
						wasm_mvp:snprintf) id="snprintf_wasm_mvp"; label="snprintf wasm mvp" ;; \
						wasm_gcplus:ryu64) id="ryu64_wasm_gcplus"; label="ryu64 wasm gcplus" ;; \
						wasm_gcplus:snprintf) id="snprintf_wasm_gcplus"; label="snprintf wasm gcplus" ;; \
						*) id=""; label="" ;; \
					esac; \
					[ -z "$$id" ] && continue; \
					printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
						"$$id" "$$label" "$$ns_per_conv" "$$elapsed_ns" "$$conversions" "$$avg_len" "$$numeric_fail" "$$bit_fail" "$$parse_fail" "$$format_fail" "$$corpus" "$$warmup" "$$random_count" "$$seed" >> $(SHOOTOUT_PERF_REPORT); \
					;; \
				FAIL) \
					printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$$profile" "$$f1" "$$f2" "$$f3" "$$f4" "$$f5" >> $(SHOOTOUT_PERF_FAILURE_REPORT); \
					;; \
			esac; \
		done < "$$file"; \
	}; \
	append_profile native "$$native_out"; \
	append_profile wasm_mvp "$$mvp_out"; \
	append_profile wasm_gcplus "$$gcplus_out"
	@echo ""
	@echo "=== shootout deep perf (single in-process pass per profile) ==="
	@tab="$$(printf '\t')"; \
	while IFS="$$tab" read -r id label ns elapsed conv avg num bit parse fmt corpus warmup random_count seed; do \
		[ "$$id" = "id" ] && continue; \
		printf "  %-24s %10s ns/conv avg_len=%s fail(num/bit/parse/fmt)=%s/%s/%s/%s\n" "$${label}:" "$$ns" "$$avg" "$$num" "$$bit" "$$parse" "$$fmt"; \
	done < $(SHOOTOUT_PERF_REPORT)
	@printf "  report file: %s\n" "$(SHOOTOUT_PERF_REPORT)"
	@printf "  failure samples: %s\n" "$(SHOOTOUT_PERF_FAILURE_REPORT)"

shootout-report-html: shootout-report-size shootout-report-perf
	@mkdir -p $(SHOOTOUT_REPORT_DIR)
	@generated="$$(date -u '+%Y-%m-%d %H:%M:%S UTC')"; \
	printf '%s\n' \
'<!doctype html>' \
'<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' \
'<title>Shootout Report</title>' \
'<style>' \
'body{font-family:"Avenir Next","Segoe UI","Helvetica Neue",Arial,sans-serif;max-width:1220px;margin:28px auto;padding:0 18px;line-height:1.45;color:#18212a;background:#fafbfc}' \
'h1{margin:0 0 10px;font-size:1.6rem;letter-spacing:0.01em}' \
'.meta{color:#4a5a6a;margin-bottom:14px}' \
'.panel{background:#fff;border:1px solid #dce3ea;border-radius:10px;box-shadow:0 6px 18px rgba(30,42,56,0.06);overflow:hidden}' \
'table{border-collapse:collapse;width:100%}' \
'thead th{background:linear-gradient(180deg,#f5f8fb,#eef3f8);font-weight:600;color:#1a2733}' \
'th,td{border-bottom:1px solid #e6edf3;padding:10px 11px;text-align:left;vertical-align:top}' \
'tbody tr:nth-child(even){background:#fcfdff}' \
'.num{white-space:nowrap;font-variant-numeric:tabular-nums;text-align:right}' \
'.flags{max-width:620px;color:#243445}' \
'.note{font-size:0.95rem;color:#405468;margin-top:10px}' \
'.ok{color:#1f6f43;font-weight:600}' \
'.warn{color:#8a4b00;font-weight:600}' \
'.muted{color:#617487}' \
'</style></head><body>' \
'<h1>Shootout Report</h1>' \
"<div class=\"meta\">Generated: $$generated</div>" \
'<div class="panel">' \
'<table><thead><tr><th>Program</th><th class="num">Size (bytes)</th><th class="num">ns/conv</th><th>Flags (compiler/linker flags)</th><th class="num">numeric failures</th><th class="num">bit-exact failures</th><th class="num">average characters</th></tr></thead><tbody>' \
> $(SHOOTOUT_HTML_REPORT)
	@tab="$$(printf '\t')"; \
	while IFS="$$tab" read -r id label bytes; do \
		[ "$$id" = "id" ] && continue; \
		ns_per_conv=""; conversions=""; avg_len=""; numeric_fail=""; bit_fail=""; \
		while IFS="$$tab" read -r perf_id perf_label ns elapsed conv avg num bit parse fmt corpus warm rand seed; do \
			[ "$$perf_id" = "id" ] && continue; \
			if [ "$$perf_id" = "$$id" ]; then \
				ns_per_conv="$$ns"; conversions="$$conv"; avg_len="$$avg"; numeric_fail="$$num"; bit_fail="$$bit"; \
				break; \
			fi; \
		done < $(SHOOTOUT_PERF_REPORT); \
		[ -z "$$ns_per_conv" ] && continue; \
			case "$$id" in \
				ryu64_native) flags='$(SHOOTOUT_NATIVE_FLAGS_NOTE)' ;; \
				snprintf_native) flags='native: -flto -fno-unwind-tables -fno-asynchronous-unwind-tables (libc snprintf baseline)' ;; \
				ryu64_wasm_mvp) flags='wasm mvp: -ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fvisibility=hidden; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' ;; \
				snprintf_wasm_mvp) flags='wasm mvp: -flto wasi-libc build; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum)' ;; \
				ryu64_wasm_gcplus) flags='wasm gcplus: -ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fvisibility=hidden +flto; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' ;; \
				snprintf_wasm_gcplus) flags='wasm gcplus: -flto wasi-libc build; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum)' ;; \
				*) flags='n/a' ;; \
			esac; \
		ncls="warn"; [ "$$numeric_fail" = "0" ] && ncls="ok"; \
		bcls="warn"; [ "$$bit_fail" = "0" ] && bcls="ok"; \
		printf '<tr><td>%s</td><td class="num js-int" data-int="%s">%s</td><td class="num js-fixed1" data-float="%s">%s</td><td class="flags">%s</td><td class="num %s js-ratio" data-num="%s" data-den="%s">%s/%s</td><td class="num %s js-ratio" data-num="%s" data-den="%s">%s/%s</td><td class="num js-fixed3" data-float="%s">%s</td></tr>\n' \
			"$$label" "$$bytes" "$$bytes" "$$ns_per_conv" "$$ns_per_conv" "$$flags" "$$ncls" "$$numeric_fail" "$$conversions" "$$numeric_fail" "$$conversions" "$$bcls" "$$bit_fail" "$$conversions" "$$bit_fail" "$$conversions" "$$avg_len" "$$avg_len" >> $(SHOOTOUT_HTML_REPORT); \
	done < $(SHOOTOUT_SIZE_REPORT)
	@tab="$$(printf '\t')"; \
	corpus_size="0"; warmup="0"; random_count="0"; seed="n/a"; \
	while IFS="$$tab" read -r id label ns elapsed conv avg num bit parse fmt corpus warm rand seed_value; do \
		[ "$$id" = "id" ] && continue; \
		corpus_size="$$corpus"; warmup="$$warm"; random_count="$$rand"; seed="$$seed_value"; \
		break; \
	done < $(SHOOTOUT_PERF_REPORT); \
	printf '%s\n' \
'</tbody></table>' \
'</div>' \
'<div class="note">Common flags (all programs): -std=c11 -DNDEBUG -ffunction-sections -fdata-sections.</div>' \
'<div class="note">WASM variants include post-processing with /opt/homebrew/bin/wasm-opt -Oz --strip-dwarf --strip-producers --vacuum when available.</div>' \
"<div class=\"note\">Deep benchmark corpus: <span class=\"js-int\" data-int=\"$$corpus_size\">$$corpus_size</span> values; warmup: <span class=\"js-int\" data-int=\"$$warmup\">$$warmup</span>; random subset: <span class=\"js-int\" data-int=\"$$random_count\">$$random_count</span>; seed: $$seed.</div>" \
'<div class="note muted">`bit-exact failures` are expected for NaN sign/payload normalization differences across format/parse paths.</div>' \
'<script>' \
'(function() {' \
'  const intFmt = new Intl.NumberFormat("en-US");' \
'  const fixed1Fmt = new Intl.NumberFormat("en-US", { minimumFractionDigits: 1, maximumFractionDigits: 1 });' \
'  const fixed3Fmt = new Intl.NumberFormat("en-US", { minimumFractionDigits: 3, maximumFractionDigits: 3 });' \
'  document.querySelectorAll(".js-int").forEach((el) => {' \
'    const value = Number(el.dataset.int);' \
'    if (Number.isFinite(value)) { el.textContent = intFmt.format(value); }' \
'  });' \
'  document.querySelectorAll(".js-fixed1").forEach((el) => {' \
'    const value = Number(el.dataset.float);' \
'    if (Number.isFinite(value)) { el.textContent = fixed1Fmt.format(value); }' \
'  });' \
'  document.querySelectorAll(".js-fixed3").forEach((el) => {' \
'    const value = Number(el.dataset.float);' \
'    if (Number.isFinite(value)) { el.textContent = fixed3Fmt.format(value); }' \
'  });' \
'  document.querySelectorAll(".js-ratio").forEach((el) => {' \
'    const num = Number(el.dataset.num);' \
'    const den = Number(el.dataset.den);' \
'    if (Number.isFinite(num) && Number.isFinite(den)) {' \
'      el.textContent = intFmt.format(num) + "/" + intFmt.format(den);' \
'    }' \
'  });' \
'})();' \
'</script>' \
'</body></html>' \
>> $(SHOOTOUT_HTML_REPORT)
	@printf "  html report: %s\n" "$(SHOOTOUT_HTML_REPORT)"

shootout-track: shootout-report-size shootout-report-perf
	@mkdir -p $(SHOOTOUT_REPORT_DIR)
	@tab="$$(printf '\t')"; \
	ts="$$(date -u '+%Y-%m-%d %H:%M:%S UTC')"; \
	commit="$$(git rev-parse --short=12 HEAD 2>/dev/null || printf '%s' 'unknown')"; \
	if git rev-parse --git-dir >/dev/null 2>&1 && ! git diff --quiet --ignore-submodules HEAD -- 2>/dev/null; then \
		commit="$${commit}-dirty"; \
	fi; \
	if [ ! -f "$(SHOOTOUT_HISTORY_REPORT)" ]; then \
		printf "timestamp_utc\tcommit\tid\tlabel\tsize_bytes\tns_per_conv\tnumeric_fail\tbit_fail\tconversions\tavg_len\tcorpus_size\twarmup\trandom_count\tseed\n" > "$(SHOOTOUT_HISTORY_REPORT)"; \
	fi; \
	while IFS="$$tab" read -r sid slabel sbytes; do \
		[ "$$sid" = "id" ] && continue; \
		found="0"; \
		while IFS="$$tab" read -r pid plabel pns pelapsed pconv pavg pnum pbit pparse pfmt pcorpus pwarm prandom pseed; do \
			[ "$$pid" = "id" ] && continue; \
			if [ "$$pid" = "$$sid" ]; then \
				printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
					"$$ts" "$$commit" "$$sid" "$$slabel" "$$sbytes" "$$pns" "$$pnum" "$$pbit" "$$pconv" "$$pavg" "$$pcorpus" "$$pwarm" "$$prandom" "$$pseed" >> "$(SHOOTOUT_HISTORY_REPORT)"; \
				found="1"; \
				break; \
			fi; \
		done < "$(SHOOTOUT_PERF_REPORT)"; \
		if [ "$$found" != "1" ]; then \
			echo "shootout track warning: missing perf row for $$sid"; \
		fi; \
	done < "$(SHOOTOUT_SIZE_REPORT)"
	@printf "  history log: %s\n" "$(SHOOTOUT_HISTORY_REPORT)"

build/shootout_ryu64_native: $(SRC_FMT_FULL) shootout/ryu64_print.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -DRYU_TIER_FULL -Iinclude -Ishootout \
		$(SRC_FMT_FULL) shootout/ryu64_print.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_ryu64_mvp_raw.wasm: $(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_MVP_CFLAGS) -DRYU_TIER_FULL -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_MVP_LDFLAGS)

build/shootout_ryu64_mvp.wasm: build/shootout_ryu64_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_ryu64_gcplus_raw.wasm: $(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_GCPLUS_CFLAGS) -DRYU_TIER_FULL -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC_FMT_FULL) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_GCPLUS_LDFLAGS)

build/shootout_ryu64_gcplus.wasm: build/shootout_ryu64_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

build/shootout_ryu64.wasm: build/shootout_ryu64_gcplus.wasm
	cp $< $@

build/shootout_snprintf_native: shootout/snprintf_print.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -Ishootout \
		shootout/snprintf_print.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_snprintf_mvp_raw.wasm: shootout/snprintf_print.c shootout/values.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_MVP_CFLAGS) -Ishootout \
		shootout/snprintf_print.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_snprintf_mvp.wasm: build/shootout_snprintf_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features --disable-bulk-memory --disable-bulk-memory-opt --llvm-memory-copy-fill-lowering $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_snprintf_gcplus_raw.wasm: shootout/snprintf_print.c shootout/values.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_GCPLUS_CFLAGS) -Ishootout \
		shootout/snprintf_print.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_snprintf_gcplus.wasm: build/shootout_snprintf_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

build/shootout_snprintf.wasm: build/shootout_snprintf_gcplus.wasm
	cp $< $@

build/shootout_deep_native: $(SRC_FMT_FULL) shootout/deep_bench.c
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -DRYU_TIER_FULL -Iinclude -Ishootout \
		$(SRC_FMT_FULL) shootout/deep_bench.c \
		-o $@ $(SIZE_LDFLAGS) $(LIBS_MATH) -flto
	strip $@

build/shootout_deep_mvp_raw.wasm: $(SRC_FMT_FULL) shootout/deep_bench.c
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_MVP_CFLAGS) -DRYU_TIER_FULL -Iinclude -Ishootout \
		$(SRC_FMT_FULL) shootout/deep_bench.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_deep_mvp.wasm: build/shootout_deep_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features --disable-bulk-memory --disable-bulk-memory-opt --llvm-memory-copy-fill-lowering $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_deep_gcplus_raw.wasm: $(SRC_FMT_FULL) shootout/deep_bench.c
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_GCPLUS_CFLAGS) -DRYU_TIER_FULL -Iinclude -Ishootout \
		$(SRC_FMT_FULL) shootout/deep_bench.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_deep_gcplus.wasm: build/shootout_deep_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

clean:
	$(RM) -r build

-include $(DEPS)
