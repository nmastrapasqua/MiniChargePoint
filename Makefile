# ===========================================================================
# MiniChargePoint — Makefile
#
# Target principali:
#   make            → compila firmware_simulator e charge_point_app
#   make test       → compila ed esegue tutti gli unit test
#   make clean      → rimuove artefatti di compilazione
#
# Requisiti: 10.1, 10.2, 11.3
# ===========================================================================

CXX       := g++
CXXFLAGS  := -std=c++17 -Wall -Wextra -Iinclude
LDFLAGS   :=

# Librerie Poco (senza CppUnit)
POCO_LIBS := -lPocoJSON -lPocoNet -lPocoUtil -lPocoFoundation

# Directory
BUILDDIR  := build
OBJDIR    := $(BUILDDIR)/obj

# ---------------------------------------------------------------------------
# Sorgenti per componente
# ---------------------------------------------------------------------------

# Firmware_Layer
FW_SRCS := $(wildcard src/firmware/*.cpp)
FW_OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(FW_SRCS))

# Application_Layer
APP_SRCS := $(wildcard src/app/*.cpp)
APP_OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(APP_SRCS))

# Common
COMMON_SRCS := $(wildcard src/common/*.cpp)
COMMON_OBJS := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(COMMON_SRCS))

# Sorgenti condivisi (common + app senza main)
SHARED_APP_OBJS := $(filter-out $(OBJDIR)/app/main_app.o,$(APP_OBJS))
SHARED_FW_OBJS  := $(filter-out $(OBJDIR)/firmware/main_firmware.o,$(FW_OBJS))

# Test
TEST_SRCS := $(wildcard test/*.cpp)
TEST_OBJS := $(patsubst test/%.cpp,$(OBJDIR)/test/%.o,$(TEST_SRCS))

# Eseguibili
FW_BIN   := $(BUILDDIR)/firmware_simulator
APP_BIN  := $(BUILDDIR)/charge_point_app

# ---------------------------------------------------------------------------
# Target principali
# ---------------------------------------------------------------------------

.PHONY: all clean test
.SECONDARY: $(TEST_OBJS)

all: $(FW_BIN) $(APP_BIN)

# firmware_simulator: sorgenti firmware + common
$(FW_BIN): $(FW_OBJS) $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(POCO_LIBS)

# charge_point_app: sorgenti app + common
$(APP_BIN): $(APP_OBJS) $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(POCO_LIBS)

# ---------------------------------------------------------------------------
# Test — ogni file test/*.cpp produce un eseguibile autonomo
# ---------------------------------------------------------------------------

# Lista degli eseguibili di test (uno per file .cpp in test/)
TEST_BINS := $(patsubst test/%.cpp,$(BUILDDIR)/test/%,$(TEST_SRCS))

test: $(TEST_BINS)
	@echo "=== Esecuzione test ==="
	@export LD_LIBRARY_PATH=/usr/local/lib:$$LD_LIBRARY_PATH; \
	fail=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		$$t || fail=1; \
	done; \
	if [ $$fail -eq 1 ]; then echo "\n*** ALCUNI TEST FALLITI ***"; exit 1; \
	else echo "\n*** TUTTI I TEST PASSATI ***"; fi

# Ogni test linka i sorgenti app + common (senza main_app/main_firmware)
$(BUILDDIR)/test/%: $(OBJDIR)/test/%.o $(SHARED_APP_OBJS) $(SHARED_FW_OBJS) $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(POCO_LIBS)

# ---------------------------------------------------------------------------
# Regole di compilazione
# ---------------------------------------------------------------------------

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR)/test/%.o: test/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# Pulizia
# ---------------------------------------------------------------------------

clean:
	rm -rf $(BUILDDIR)
