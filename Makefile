CC ?= cc
AR ?= ar
RM ?= rm -f
WASM_CC ?= clang

WASI_SDK ?= /Users/mshonle/Projects/lambkin-lang/build-tools/wasi-sdk
WASI_SYSROOT ?= $(WASI_SDK)/share/wasi-sysroot
WASI_CC_CMD = $(WASI_SDK)/bin/clang --target=wasm32-wasi --sysroot=$(WASI_SYSROOT)
WASI_OPT_LEVEL ?= z
WASI_ENABLE_LTO ?= 1

# Common flag fragments — single source of truth
COMMON_WARNINGS    = -Wall -Wextra -Wpedantic
FREESTANDING_FLAGS = -ffreestanding -fno-builtin -nostdinc
GC_SECTION_FLAGS   = -ffunction-sections -fdata-sections
WASM_HARDENING     = -fno-stack-protector -fvisibility=hidden
SHOOTOUT_STRIP     = -fno-unwind-tables -fno-asynchronous-unwind-tables

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
WASI_CFLAGS = -std=c11 -O$(WASI_OPT_LEVEL) -DNDEBUG $(COMMON_WARNINGS) $(FREESTANDING_FLAGS) \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) \
	-Iwasm_compat -Iinclude -Iwasi
WASI_BUILTINS = $(shell $(WASI_SDK)/bin/clang --target=wasm32-wasi -print-libgcc-file-name 2>/dev/null)
WASI_LDFLAGS = -nostdlib -Wl,--entry=_start -Wl,--export=_start -Wl,--gc-sections -Wl,--strip-all
WASI_MVP_CFLAGS = -std=c11 -Oz -DNDEBUG $(COMMON_WARNINGS) $(FREESTANDING_FLAGS) \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) \
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
WASM_TOOLS ?= wasm-tools
WIT_BINDGEN ?= wit-bindgen

UNAME_S := $(shell uname -s)

CFLAGS_BASE ?= -std=c11 -O2 $(COMMON_WARNINGS) -Iinclude
WASM_CFLAGS_BASE ?= -std=c11 -Oz $(COMMON_WARNINGS) $(FREESTANDING_FLAGS) -Iwasm_compat -Iinclude
LDFLAGS_BASE ?=

SPEED_CFLAGS ?= -std=c11 -O3 -DNDEBUG $(COMMON_WARNINGS) -Iinclude
SIZE_CFLAGS ?= -std=c11 -Oz -DNDEBUG $(COMMON_WARNINGS) $(GC_SECTION_FLAGS) -Iinclude
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

SRC_FMT = \
	src/ryu64_bigint.c \
	src/ryu64_shortest.c \
	src/ryu64_tables_shortest.c \
	src/ryu64_printf.c \
	src/ryu64_tables_printf.c

SRC_PARSE = \
	src/ryu64_parse_tiny.c \
	src/ryu64_parse_full.c \
	src/ryu64_parse_tables.c

SRC = $(SRC_FMT) $(SRC_PARSE)

OBJ = $(SRC:src/%.c=build/lib/%.o)
OBJ_TEST = $(SRC:src/%.c=build/test/%.o)
OBJ_ORACLE_SPEED = $(SRC:src/%.c=build/oracle-speed/%.o) build/oracle-speed/oracle_stdio.o
OBJ_ORACLE_SIZE = $(SRC:src/%.c=build/oracle-size/%.o) build/oracle-size/oracle_stdio.o
OBJ_NOLIBC_SPEED = $(SRC:src/%.c=build/nolibc-speed/%.o) build/nolibc-speed/test_ryu64.o
OBJ_NOLIBC_SIZE = $(SRC:src/%.c=build/nolibc-size/%.o) build/nolibc-size/test_ryu64.o
OBJ_WASM_COMPAT_SPEED = build/nolibc-speed/wasm_compat_string.o
OBJ_WASM_COMPAT_SIZE = build/nolibc-size/wasm_compat_string.o
OBJ_WASI_MVP = $(SRC:src/%.c=build/wasi-mvp/%.o) \
	build/wasi-mvp/wasm_compat_string.o \
	build/wasi-mvp/wasi_start.o \
	build/wasi-mvp/smoke_main.o
OBJ_WASI_GCPLUS = $(SRC:src/%.c=build/wasi-gcplus/%.o) \
	build/wasi-gcplus/wasm_compat_string.o \
	build/wasi-gcplus/wasi_start.o \
	build/wasi-gcplus/smoke_main.o
DEPS = \
	$(OBJ:.o=.d) \
	$(OBJ_TEST:.o=.d) \
	$(OBJ_ORACLE_SPEED:.o=.d) \
	$(OBJ_ORACLE_SIZE:.o=.d) \
	$(OBJ_NOLIBC_SPEED:.o=.d) \
	$(OBJ_NOLIBC_SIZE:.o=.d) \
	$(OBJ_WASM_COMPAT_SPEED:.o=.d) \
	$(OBJ_WASM_COMPAT_SIZE:.o=.d) \
	$(OBJ_WASI_MVP:.o=.d) \
	$(OBJ_WASI_GCPLUS:.o=.d) \
	build/test/test_ryu64.d

.PHONY: all test oracle-test benchmark-speed benchmark-size nolibc-check-speed nolibc-check-size wit-check wit-bindings wasm-mvp wasm-gcplus wasm-run wasm-run-mvp wasm-run-gcplus wasm-compare shootout shootout-bench shootout-report shootout-report-size shootout-report-perf shootout-report-html shootout-track stride-investigation stride-investigation-data stride-investigation-html gen-parse-pow10 gen-parse-pow5 clean

all: build/libryu64.a

test: build/test_ryu64 build/oracle_ryu64_speed
	./build/test_ryu64
	./build/oracle_ryu64_speed --quick

oracle-test: build/oracle_ryu64_speed
	./build/oracle_ryu64_speed --quick

benchmark-speed: build/oracle_ryu64_speed
	./build/oracle_ryu64_speed

benchmark-size: build/oracle_ryu64_size
	./build/oracle_ryu64_size

nolibc-check-speed: $(OBJ_NOLIBC_SPEED) $(OBJ_WASM_COMPAT_SPEED)

nolibc-check-size: $(OBJ_NOLIBC_SIZE) $(OBJ_WASM_COMPAT_SIZE)

wit-check: wit/floats.wit
	$(WASM_TOOLS) component wit wit/floats.wit -o /dev/null
	@echo "WIT validation passed"

wit-bindings: wit/floats.wit
	@mkdir -p build/wit-bindings
	$(WIT_BINDGEN) c wit/floats.wit -w floats-impl --out-dir build/wit-bindings --no-object-file

WIT_IMPL_CFLAGS = $(CFLAGS_BASE) -Ibuild/wit-bindings -DRYU64_ENABLE_PARSE_BIGINT

build/wit-bindings/floats_impl.o: build/wit-bindings/floats_impl.c build/wit-bindings/floats_impl.h
	$(CC) $(WIT_IMPL_CFLAGS) -c $< -o $@

build/wit-impl/floats_impl.o: wit/floats_impl.c build/wit-bindings/floats_impl.h include/ryu64.h
	@mkdir -p build/wit-impl
	$(CC) $(WIT_IMPL_CFLAGS) -c $< -o $@

build/wit-impl/test_wit.o: test/test_wit.c build/wit-bindings/floats_impl.h
	@mkdir -p build/wit-impl
	$(CC) $(WIT_IMPL_CFLAGS) -c $< -o $@

build/wit-test: build/wit-bindings/floats_impl.o build/wit-impl/floats_impl.o build/wit-impl/test_wit.o $(OBJ_TEST)
	$(CC) $(WIT_IMPL_CFLAGS) -o $@ $^

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

gen-parse-pow5: build/gen_pow5_128
	./build/gen_pow5_128

build/libryu64.a: $(OBJ)
	@mkdir -p build
	$(AR) rcs $@ $(OBJ)

build/test_ryu64: $(OBJ_TEST) build/test/test_ryu64.o
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) $(LDFLAGS_BASE) -o $@ $(OBJ_TEST) build/test/test_ryu64.o $(LIBS_TEST)

build/oracle_ryu64_speed: $(OBJ_ORACLE_SPEED)
	@mkdir -p build
	$(CC) $(SPEED_CFLAGS) $(LDFLAGS_BASE) -o $@ $(OBJ_ORACLE_SPEED) $(LIBS_ORACLE)

build/oracle_ryu64_size: $(OBJ_ORACLE_SIZE)
	@mkdir -p build
	$(CC) $(SIZE_CFLAGS) $(LDFLAGS_BASE) $(SIZE_LDFLAGS) -o $@ $(OBJ_ORACLE_SIZE) $(LIBS_ORACLE)

build/lib/%.o: src/%.c
	@mkdir -p build/lib
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/%.o: src/%.c
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -DRYU_TEST_INSTRUMENTATION -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/test/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/test
	$(CC) $(CFLAGS_BASE) $(DEPFLAGS) -Isrc -DRYU_TEST_INSTRUMENTATION -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

build/oracle-speed/%.o: src/%.c
	@mkdir -p build/oracle-speed
	$(CC) $(SPEED_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_TEST_INSTRUMENTATION -c $< -o $@

build/oracle-size/%.o: src/%.c
	@mkdir -p build/oracle-size
	$(CC) $(SIZE_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_TEST_INSTRUMENTATION -c $< -o $@

build/oracle-speed/oracle_stdio.o: test/oracle_stdio.c
	@mkdir -p build/oracle-speed
	$(CC) $(SPEED_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_TEST_INSTRUMENTATION -c $< -o $@

build/oracle-size/oracle_stdio.o: test/oracle_stdio.c
	@mkdir -p build/oracle-size
	$(CC) $(SIZE_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_TEST_INSTRUMENTATION -c $< -o $@

NOLIBC_SPEED_CFLAGS = --target=wasm32 -std=c11 -O3 -DNDEBUG $(FREESTANDING_FLAGS) -Iwasm_compat -Iinclude
NOLIBC_SIZE_CFLAGS  = --target=wasm32 -std=c11 -Oz -DNDEBUG $(FREESTANDING_FLAGS) -Iwasm_compat -Iinclude

build/nolibc-speed/%.o: src/%.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) $(NOLIBC_SPEED_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/%.o: src/%.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) $(NOLIBC_SIZE_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-speed/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) $(NOLIBC_SPEED_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-speed/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/nolibc-speed
	$(WASM_CC) $(NOLIBC_SPEED_CFLAGS) $(DEPFLAGS) -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/test_ryu64.o: test/test_ryu64.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) $(NOLIBC_SIZE_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU_NO_LIBC_TEST -c $< -o $@

build/nolibc-size/wasm_compat_string.o: wasm_compat/string.c
	@mkdir -p build/nolibc-size
	$(WASM_CC) $(NOLIBC_SIZE_CFLAGS) $(DEPFLAGS) -DRYU_NO_LIBC_TEST -c $< -o $@

build/wasi-mvp/%.o: src/%.c
	@mkdir -p build/wasi-mvp
	$(WASI_CC_CMD) $(WASI_MVP_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

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
	$(WASI_CC_CMD) $(WASI_GCPLUS_CFLAGS) $(DEPFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -c $< -o $@

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

build/gen_pow5_128: tools/gen_pow5_128.c
	@mkdir -p build
	$(CC) $(CFLAGS_BASE) -o $@ $<

# --- shootout: ryu64-full vs snprintf size comparison ---

SHOOTOUT_NATIVE_CFLAGS = -std=c11 -O2 -DNDEBUG -flto $(GC_SECTION_FLAGS) $(SHOOTOUT_STRIP)
SHOOTOUT_NATIVE_ENABLE_POW5_CACHE ?= 1
SHOOTOUT_NATIVE_POW5_STRIDE ?= 16
SHOOTOUT_NATIVE_COMMON_FLAGS_NOTE = native: -flto $(SHOOTOUT_STRIP)
SHOOTOUT_NATIVE_FLAGS_NOTE = $(SHOOTOUT_NATIVE_COMMON_FLAGS_NOTE)
SHOOTOUT_WASM_ENABLE_POW5_CACHE ?= 1
SHOOTOUT_WASM_POW5_STRIDE ?= 16
SHOOTOUT_WASM_POW5_FLAGS =
SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX =
SHOOTOUT_REPORT_DIR ?= build/reports
SHOOTOUT_SIZE_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_size.tsv
SHOOTOUT_SIZE_ROUNDTRIP_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_size_roundtrip.tsv
SHOOTOUT_PERF_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_perf.tsv
SHOOTOUT_PERF_FAILURE_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_perf_failures.tsv
SHOOTOUT_HTML_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_report.html
SHOOTOUT_HISTORY_REPORT ?= $(SHOOTOUT_REPORT_DIR)/shootout_history.tsv
SHOOTOUT_COMMON_NOTE = Common flags (all programs): -std=c11 -DNDEBUG $(GC_SECTION_FLAGS).
SHOOTOUT_WASM_OPT_NOTE = WASM variants include post-processing with $(WASM_OPT) $(WASM_OPT_POST_FLAGS) when available.
SHOOTOUT_DEEP_RANDOM ?= 224624
SHOOTOUT_DEEP_SEED ?= 0x9e3779b97f4a7c15
SHOOTOUT_DEEP_WARMUP ?= 1000
SHOOTOUT_DEEP_SAMPLES ?= 8
SHOOTOUT_DEEP_ARGS = --random $(SHOOTOUT_DEEP_RANDOM) --seed $(SHOOTOUT_DEEP_SEED) --warmup $(SHOOTOUT_DEEP_WARMUP) --samples $(SHOOTOUT_DEEP_SAMPLES)

# --- stride investigation: pow5 cache size/speed tradeoff ---
STRIDE_INVESTIGATION_DIR = build/stride-investigation
STRIDE_INVESTIGATION_TSV = $(SHOOTOUT_REPORT_DIR)/stride_investigation.tsv
STRIDE_INVESTIGATION_HTML = $(SHOOTOUT_REPORT_DIR)/stride_investigation.html
STRIDE_INVESTIGATION_VALUES ?= 0 4 8 16 32 64 128
STRIDE_NATIVE_BASE_CFLAGS = -std=c11 -O2 -DNDEBUG -flto $(GC_SECTION_FLAGS) $(SHOOTOUT_STRIP)
STRIDE_WASI_FMT_BASE_CFLAGS = -std=c11 -Oz -DNDEBUG $(FREESTANDING_FLAGS) \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) $(SHOOTOUT_STRIP) $(WASI_MVP_FEATURE_FLAGS)
STRIDE_WASI_DEEP_BASE_CFLAGS = -std=c11 -Oz -DNDEBUG -flto \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) $(SHOOTOUT_STRIP) $(WASI_MVP_FEATURE_FLAGS)

SHOOTOUT_WASI_MVP_CFLAGS = -std=c11 -Oz -DNDEBUG $(FREESTANDING_FLAGS) \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) $(SHOOTOUT_STRIP) $(WASI_MVP_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_GCPLUS_CFLAGS = -std=c11 -Oz -DNDEBUG $(FREESTANDING_FLAGS) \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) $(SHOOTOUT_STRIP) $(WASI_GCPLUS_FEATURE_FLAGS) $(SHOOTOUT_WASM_POW5_FLAGS)
SHOOTOUT_WASI_LIBC_CFLAGS = -std=c11 -Oz -DNDEBUG -flto \
	$(GC_SECTION_FLAGS) $(WASM_HARDENING) $(SHOOTOUT_STRIP)
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

shootout-report-size: build/shootout_ryu64_native build/shootout_ryu64_mvp.wasm build/shootout_ryu64_gcplus.wasm build/shootout_snprintf_native build/shootout_snprintf_mvp.wasm build/shootout_snprintf_gcplus.wasm build/shootout_ryu64_rt_native build/shootout_ryu64_rt_mvp.wasm build/shootout_ryu64_rt_gcplus.wasm build/shootout_snprintf_rt_native build/shootout_snprintf_rt_mvp.wasm build/shootout_snprintf_rt_gcplus.wasm
	@BUILD_DIR=build \
	 SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 SHOOTOUT_SIZE_REPORT='$(SHOOTOUT_SIZE_REPORT)' \
	 SHOOTOUT_SIZE_ROUNDTRIP_REPORT='$(SHOOTOUT_SIZE_ROUNDTRIP_REPORT)' \
	 tools/shootout_size.sh

shootout-report-perf: build/shootout_deep_native build/shootout_deep_mvp.wasm build/shootout_deep_gcplus.wasm
	@BUILD_DIR=build \
	 SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 SHOOTOUT_PERF_REPORT='$(SHOOTOUT_PERF_REPORT)' \
	 SHOOTOUT_PERF_FAILURE_REPORT='$(SHOOTOUT_PERF_FAILURE_REPORT)' \
	 SHOOTOUT_DEEP_ARGS='$(SHOOTOUT_DEEP_ARGS)' \
	 WASMTIME='$(WASMTIME)' \
	 tools/shootout_perf.sh

shootout-report-html: shootout-report-size shootout-report-perf
	@SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 SHOOTOUT_SIZE_REPORT='$(SHOOTOUT_SIZE_REPORT)' \
	 SHOOTOUT_SIZE_ROUNDTRIP_REPORT='$(SHOOTOUT_SIZE_ROUNDTRIP_REPORT)' \
	 SHOOTOUT_PERF_REPORT='$(SHOOTOUT_PERF_REPORT)' \
	 SHOOTOUT_HTML_REPORT='$(SHOOTOUT_HTML_REPORT)' \
	 SHOOTOUT_COMMON_NOTE='$(SHOOTOUT_COMMON_NOTE)' \
	 SHOOTOUT_WASM_OPT_NOTE='$(SHOOTOUT_WASM_OPT_NOTE)' \
	 SHOOTOUT_FLAGS_MAP="$$(printf '%s\n' \
		'ryu64_native	native clean-room formatter ($(SHOOTOUT_NATIVE_FLAGS_NOTE))' \
		'snprintf_native	native libc snprintf baseline ($(SHOOTOUT_NATIVE_COMMON_FLAGS_NOTE))' \
		'ryu64_wasm_mvp	wasm mvp clean-room formatter; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' \
		'snprintf_wasm_mvp	wasm mvp libc snprintf baseline; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum)' \
		'ryu64_wasm_gcplus	wasm gcplus clean-room formatter; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' \
		'snprintf_wasm_gcplus	wasm gcplus libc snprintf baseline; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum)' \
		'ryu64_rt_native	native clean-room formatter + ryu64_from_decimal_full parser ($(SHOOTOUT_NATIVE_FLAGS_NOTE))' \
		'snprintf_rt_native	native libc snprintf + strtod parser ($(SHOOTOUT_NATIVE_COMMON_FLAGS_NOTE))' \
		'ryu64_rt_wasm_mvp	wasm mvp clean-room formatter + ryu64_from_decimal_full parser; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' \
		'snprintf_rt_wasm_mvp	wasm mvp libc snprintf + strtod via wasi-libc; MVP feature disables; wasm-opt mvp lowering (+ optional strip/vacuum)' \
		'ryu64_rt_wasm_gcplus	wasm gcplus clean-room formatter + ryu64_from_decimal_full parser; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum) $(SHOOTOUT_WASM_FLAGS_NOTE_SUFFIX)' \
		'snprintf_rt_wasm_gcplus	wasm gcplus libc snprintf + strtod via wasi-libc; GC+ feature enables; wasm-opt -Oz (+ optional strip/vacuum)' \
	 )" \
	 tools/shootout_html.sh

shootout-track: shootout-report-size shootout-report-perf
	@SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 SHOOTOUT_SIZE_REPORT='$(SHOOTOUT_SIZE_REPORT)' \
	 SHOOTOUT_SIZE_ROUNDTRIP_REPORT='$(SHOOTOUT_SIZE_ROUNDTRIP_REPORT)' \
	 SHOOTOUT_PERF_REPORT='$(SHOOTOUT_PERF_REPORT)' \
	 SHOOTOUT_HISTORY_REPORT='$(SHOOTOUT_HISTORY_REPORT)' \
	 tools/shootout_track.sh

# --- stride investigation ---

stride-investigation: stride-investigation-html
	@printf "  tsv:  %s\n" "$(STRIDE_INVESTIGATION_TSV)"
	@printf "  html: %s\n" "$(STRIDE_INVESTIGATION_HTML)"

stride-investigation-data:
	@CC='$(CC)' \
	 WASI_CC_CMD='$(WASI_CC_CMD)' \
	 WASM_OPT='$(WASM_OPT)' \
	 WASM_OPT_POST_FLAGS='$(WASM_OPT_POST_FLAGS)' \
	 WASMTIME='$(WASMTIME)' \
	 WASI_BUILTINS='$(WASI_BUILTINS)' \
	 STRIDE_NATIVE_BASE_CFLAGS='$(STRIDE_NATIVE_BASE_CFLAGS)' \
	 STRIDE_WASI_FMT_BASE_CFLAGS='$(STRIDE_WASI_FMT_BASE_CFLAGS)' \
	 STRIDE_WASI_DEEP_BASE_CFLAGS='$(STRIDE_WASI_DEEP_BASE_CFLAGS)' \
	 SHOOTOUT_WASI_MVP_LDFLAGS='$(SHOOTOUT_WASI_MVP_LDFLAGS)' \
	 SIZE_LDFLAGS='$(SIZE_LDFLAGS)' \
	 LIBS_MATH='$(LIBS_MATH)' \
	 SRC_FMT='$(SRC_FMT)' \
	 SRC='$(SRC)' \
	 SHOOTOUT_DEEP_ARGS='$(SHOOTOUT_DEEP_ARGS)' \
	 STRIDE_INVESTIGATION_DIR='$(STRIDE_INVESTIGATION_DIR)' \
	 SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 STRIDE_INVESTIGATION_TSV='$(STRIDE_INVESTIGATION_TSV)' \
	 STRIDE_INVESTIGATION_VALUES='$(STRIDE_INVESTIGATION_VALUES)' \
	 tools/stride_report.sh data

stride-investigation-html: stride-investigation-data
	@SHOOTOUT_REPORT_DIR='$(SHOOTOUT_REPORT_DIR)' \
	 STRIDE_INVESTIGATION_TSV='$(STRIDE_INVESTIGATION_TSV)' \
	 STRIDE_INVESTIGATION_HTML='$(STRIDE_INVESTIGATION_HTML)' \
	 tools/stride_report.sh html

build/shootout_ryu64_native: $(SRC_FMT) shootout/ryu64_print.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -Iinclude -Ishootout \
		$(SRC_FMT) shootout/ryu64_print.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_ryu64_mvp_raw.wasm: $(SRC_FMT) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_MVP_CFLAGS) -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC_FMT) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_MVP_LDFLAGS)

build/shootout_ryu64_mvp.wasm: build/shootout_ryu64_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_ryu64_gcplus_raw.wasm: $(SRC_FMT) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_GCPLUS_CFLAGS) -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC_FMT) shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_GCPLUS_LDFLAGS)

build/shootout_ryu64_gcplus.wasm: build/shootout_ryu64_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

build/shootout_ryu64.wasm: build/shootout_ryu64_gcplus.wasm
	cp $< $@

build/shootout_ryu64_rt_native: $(SRC) shootout/ryu64_roundtrip.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
		$(SRC) shootout/ryu64_roundtrip.c \
		-o $@ $(SIZE_LDFLAGS) $(LIBS_MATH) -flto
	strip $@

build/shootout_ryu64_rt_mvp_raw.wasm: $(SRC) shootout/ryu64_roundtrip.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_MVP_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC) shootout/ryu64_roundtrip.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_MVP_LDFLAGS)

build/shootout_ryu64_rt_mvp.wasm: build/shootout_ryu64_rt_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_ryu64_rt_gcplus_raw.wasm: $(SRC) shootout/ryu64_roundtrip.c wasi/wasi_start.c wasm_compat/string.c shootout/values.h wasi/wasi_io.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_GCPLUS_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -DRYU64_WASI_BUILD \
		-Iwasm_compat -Iinclude -Iwasi -Ishootout \
		$(SRC) shootout/ryu64_roundtrip.c wasi/wasi_start.c wasm_compat/string.c \
		$(WASI_BUILTINS) -o $@ \
		$(SHOOTOUT_WASI_GCPLUS_LDFLAGS)

build/shootout_ryu64_rt_gcplus.wasm: build/shootout_ryu64_rt_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

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

build/shootout_snprintf_rt_native: shootout/snprintf_roundtrip.c shootout/values.h
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -Ishootout \
		shootout/snprintf_roundtrip.c \
		-o $@ $(SIZE_LDFLAGS) -flto
	strip $@

build/shootout_snprintf_rt_mvp_raw.wasm: shootout/snprintf_roundtrip.c shootout/values.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_MVP_CFLAGS) -Ishootout \
		shootout/snprintf_roundtrip.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_snprintf_rt_mvp.wasm: build/shootout_snprintf_rt_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features --disable-bulk-memory --disable-bulk-memory-opt --llvm-memory-copy-fill-lowering $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_snprintf_rt_gcplus_raw.wasm: shootout/snprintf_roundtrip.c shootout/values.h
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_GCPLUS_CFLAGS) -Ishootout \
		shootout/snprintf_roundtrip.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_snprintf_rt_gcplus.wasm: build/shootout_snprintf_rt_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

build/shootout_deep_native: $(SRC) shootout/deep_bench.c
	@mkdir -p build
	$(CC) $(SHOOTOUT_NATIVE_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
		$(SRC) shootout/deep_bench.c \
		-o $@ $(SIZE_LDFLAGS) $(LIBS_MATH) -flto
	strip $@

build/shootout_deep_mvp_raw.wasm: $(SRC) shootout/deep_bench.c
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_MVP_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
		$(SRC) shootout/deep_bench.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_deep_mvp.wasm: build/shootout_deep_mvp_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) --strip-target-features --disable-bulk-memory --disable-bulk-memory-opt --llvm-memory-copy-fill-lowering $< -o $@
	wasm-tools validate --features=mvp $@

build/shootout_deep_gcplus_raw.wasm: $(SRC) shootout/deep_bench.c
	@mkdir -p build
	$(WASI_CC_CMD) $(SHOOTOUT_WASI_LIBC_GCPLUS_CFLAGS) -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
		$(SRC) shootout/deep_bench.c \
		-o $@ -Wl,--gc-sections -Wl,--strip-all -flto

build/shootout_deep_gcplus.wasm: build/shootout_deep_gcplus_raw.wasm
	$(WASM_OPT) $(WASM_OPT_POST_FLAGS) $< -o $@

lint:
	@echo "Linting sources..."
	clang-tidy $(SRC) -- $(CFLAGS_BASE) -Isrc

clean:
	$(RM) -r build

-include $(DEPS)
