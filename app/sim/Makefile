# Cardputer UI simulator - plain make + g++, no PlatformIO, no SDL needed.
#   make          build build/cardputer_sim (downloads LovyanGFX once)
#   make run      run every scenario in scenarios/, screenshots in build/shots/
#   make readme-images   screenshots of the README (scenarios/readme_*.txt) -> ../../docs/images/
#   make test     unit tests of the drivers against fake chips (tests/, fakes/), no LovyanGFX needed
#   make wire     real MeshCore code on a simulated air (wire/test_wire.cpp): packets, repeaters, regions, path sizes
#   make dbtest   the SQLite trial of docs/storage on the PC, with the device library's SQLite (dbtest/)
#   make clean      remove the build outputs (keeps LovyanGFX); make distclean removes everything
LGFX_VERSION ?= 1.2.7
BUILD        ?= build
LGFX_DIR     ?= $(BUILD)/LovyanGFX
CXX          ?= g++
CC           ?= gcc

CXXFLAGS ?= -O1 -g
CXXFLAGS += -std=c++17 -Wall -Wno-unused-function -DLGFX_USE_V1 -DLGFX_LINUX_FB -I$(LGFX_DIR)/src
CFLAGS   ?= -O1
CFLAGS   += -std=gnu17 -I$(LGFX_DIR)/src

CORE_SRC  := $(wildcard ../core/*.cpp ../core/screens/*.cpp ../core/drivers/*.cpp)
SIM_SRC   := $(wildcard *.cpp fakes/*.cpp)
APP_OBJ   := $(patsubst ../core/%.cpp,$(BUILD)/core/%.o,$(CORE_SRC)) $(patsubst %.cpp,$(BUILD)/sim/%.o,$(SIM_SRC))
HDRS      := $(wildcard *.h fakes/*.h ../core/*.h ../core/screens/*.h ../core/drivers/*.h ../core/hal/*.h)

# Unit tests: drivers + pure decoders only (no screens, no LovyanGFX)
TEST_SRC  := $(wildcard tests/*.cpp)
TEST_OBJ  := $(patsubst %.cpp,$(BUILD)/sim/%.o,$(TEST_SRC)) \
             $(patsubst ../core/%.cpp,$(BUILD)/test_core/%.o,$(wildcard ../core/drivers/*.cpp) ../core/KeyMatrix.cpp ../core/Nmea.cpp ../core/MessageStore.cpp) \
             $(BUILD)/sim/SimLog.o $(patsubst %.cpp,$(BUILD)/sim/%.o,$(wildcard fakes/*.cpp))

LGFX_STAMP := $(LGFX_DIR)/.stamp
LGFX_LIB   := $(BUILD)/liblgfx.a

SCENARIOS := $(wildcard scenarios/*.txt)

all: $(BUILD)/cardputer_sim

$(LGFX_STAMP):
	rm -rf $(LGFX_DIR)
	git clone -q --depth 1 --branch $(LGFX_VERSION) https://github.com/lovyan03/LovyanGFX $(LGFX_DIR)
	touch $@

# LovyanGFX is built once into a static archive (platform-independent parts + framebuffer stubs).
# Its CJK font tables are compiled too because lgfx_fonts.cpp references them all.
$(LGFX_LIB): $(LGFX_STAMP)
	@mkdir -p $(BUILD)/lgfx
	@set -e; for f in $$(find $(LGFX_DIR)/src/lgfx -name '*.cpp' -not -path '*/platforms/*') \
	                $(LGFX_DIR)/src/lgfx/v1/platforms/framebuffer/common.cpp; do \
	  o=$(BUILD)/lgfx/$$(echo $$f | sed 's|/|_|g').o; \
	  $(CXX) $(CXXFLAGS) -w -c $$f -o $$o; done
	@set -e; for f in $$(find $(LGFX_DIR)/src/lgfx -name '*.c'); do \
	  o=$(BUILD)/lgfx/$$(echo $$f | sed 's|/|_|g').o; \
	  $(CC) $(CFLAGS) -w -c $$f -o $$o; done
	ar rcs $@ $(BUILD)/lgfx/*.o

$(BUILD)/core/%.o: ../core/%.cpp $(LGFX_STAMP) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_core/%.o: ../core/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim/tests/%.o: tests/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim/SimLog.o: SimLog.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim/fakes/%.o: fakes/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim/%.o: %.cpp $(LGFX_STAMP) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/unit_tests: $(TEST_OBJ)
	$(CXX) -o $@ $(TEST_OBJ)

test: $(BUILD)/unit_tests
	$(BUILD)/unit_tests

$(BUILD)/cardputer_sim: $(APP_OBJ) $(LGFX_LIB)
	$(CXX) -o $@ $(APP_OBJ) $(LGFX_LIB) -lpthread -Wl,--gc-sections

# Wire tests: meshcore/src compiled unchanged, with the firmware's own Crypto library (installed by PlatformIO:
# `pio pkg install -e cardputer_adv_standalone`), on a small Arduino stub
MESHCORE    := ../../meshcore
LIBDEPS     ?= ../../.pio/libdeps/cardputer_adv_standalone
CRYPTO_DIR  ?= $(LIBDEPS)/Crypto
WIRE_FLAGS  := -std=c++17 -O1 -g -w -DRP2040_PLATFORM -DARDUINO=100 -DMAX_CONTACTS=32 -DMAX_GROUP_CHANNELS=4 -Iwire/stubs -I$(MESHCORE)/src -I$(MESHCORE)/lib/ed25519 -I$(CRYPTO_DIR) -I$(LIBDEPS)/base64/src
WIRE_SRC    := Dispatcher.cpp Identity.cpp Mesh.cpp Packet.cpp Utils.cpp AdvertDataHelpers.cpp BaseChatMesh.cpp RegionMap.cpp \
               StaticPoolPacketManager.cpp TransportKeyStore.cpp TxtDataHelpers.cpp test_wire.cpp ContactFile.cpp ContactPositions.cpp GeoText.cpp FloodPolicy.cpp FloodOptions.cpp MessageRoute.cpp PacketLog.cpp PathView.cpp PositionBook.cpp ContactPosition.cpp
WIRE_CRYPTO := AES128.cpp AESCommon.cpp BlockCipher.cpp ChaCha.cpp Cipher.cpp Crypto.cpp Curve25519.cpp BigNumberUtil.cpp Ed25519.cpp Hash.cpp SHA256.cpp SHA512.cpp RNG.cpp
WIRE_C      := $(wildcard $(MESHCORE)/lib/ed25519/*.c)
WIRE_DIR    := $(BUILD)/wire
WIRE_OBJ    := $(patsubst %.cpp,$(WIRE_DIR)/%.o,$(WIRE_SRC)) $(patsubst %.cpp,$(WIRE_DIR)/crypto_%.o,$(WIRE_CRYPTO)) \
               $(patsubst %.c,$(WIRE_DIR)/ed_%.o,$(notdir $(WIRE_C)))

vpath %.cpp $(MESHCORE)/src $(MESHCORE)/src/helpers wire ../device ../core/drivers

# No platform here: the library then uses its plain C++ code, not a chip's
$(WIRE_DIR)/crypto_%.o: $(CRYPTO_DIR)/%.cpp
	@mkdir -p $(WIRE_DIR)
	$(CXX) $(filter-out -DRP2040_PLATFORM -DARDUINO=100 -DMAX_CONTACTS=32 -DMAX_GROUP_CHANNELS=4,$(WIRE_FLAGS)) -c $< -o $@

$(WIRE_DIR)/ed_%.o: $(MESHCORE)/lib/ed25519/%.c
	@mkdir -p $(WIRE_DIR)
	$(CC) -O1 -w -c $< -o $@

$(WIRE_DIR)/%.o: %.cpp $(HDRS) wire/stubs/Arduino.h ../device/FloodPolicy.h
	@mkdir -p $(WIRE_DIR)
	$(CXX) $(WIRE_FLAGS) -c $< -o $@

$(WIRE_DIR)/wire_tests: $(WIRE_OBJ)
	$(CXX) -o $@ $(WIRE_OBJ)

wire: $(WIRE_DIR)/wire_tests
	$(WIRE_DIR)/wire_tests

# SQLite trial: the SQLite sources of the device library (siara-cc/Sqlite3Esp32, installed by PlatformIO for the env
# cardputer_adv_dual_dbtest). Its sqlite3.c includes its ESP32 config_ext.h first: compiled from a copy placed next to a
# copy of its configuration without the ESP32 file access, so memory figures are the device's and the library itself
# stays untouched.
SQLITE_SRC := ../../.pio/libdeps/cardputer_adv_dual_dbtest/Sqlite3Esp32/src
DBT        := $(BUILD)/dbtest

$(DBT)/sqlite3.c: $(SQLITE_SRC)/sqlite3.c
	@mkdir -p $(DBT)
	cp $(SQLITE_SRC)/sqlite3.c $(SQLITE_SRC)/sqlite3.h $(DBT)/
	grep -v -E 'SQLITE_OS_OTHER|SQLITE_MUTEX_APPDEF' $(SQLITE_SRC)/config_ext.h > $(DBT)/config_ext.h   # the device settings, the PC's file access

$(DBT)/sqlite3.o: $(DBT)/sqlite3.c
	$(CC) -O1 -w -c $< -o $@

$(DBT)/DbProbe.o: ../device/DbProbe.cpp ../device/DbProbe.h ../device/DbVfsMeter.h $(DBT)/sqlite3.c
	$(CXX) -std=c++17 -O1 -g -Wall -DDBPROBE_HOST -I$(DBT) -c $< -o $@

$(DBT)/DbVfsMeter.o: ../device/DbVfsMeter.cpp ../device/DbVfsMeter.h $(DBT)/sqlite3.c
	$(CXX) -std=c++17 -O1 -g -Wall -DDBPROBE_HOST -I$(DBT) -c $< -o $@

$(DBT)/host_dbtest.o: dbtest/host_dbtest.cpp ../device/DbProbe.h $(DBT)/sqlite3.c
	$(CXX) -std=c++17 -O1 -g -Wall -I$(DBT) -c $< -o $@

$(DBT)/dbtest: $(DBT)/sqlite3.o $(DBT)/DbProbe.o $(DBT)/DbVfsMeter.o $(DBT)/host_dbtest.o
	$(CXX) -o $@ $^ -lpthread -lm

dbtest: $(DBT)/dbtest
	$(DBT)/dbtest

TILES_STAMP := $(BUILD)/maptiles/tiles/info.txt
$(TILES_STAMP): ../../tools/tiles/make_tiles.py
	python3 ../../tools/tiles/make_tiles.py --center 43.6116 1.3346 --radius-km 1.5 --zooms 13-15 --synthetic --out $(BUILD)/maptiles

run: $(BUILD)/cardputer_sim $(TILES_STAMP)
	@mkdir -p $(BUILD)/shots
	@set -e; for s in $(SCENARIOS); do echo "== $$s"; $(BUILD)/cardputer_sim $$s $(BUILD)/shots; done
	@mkdir -p $(BUILD)/shots_hw
	@set -e; for s in $(SCENARIOS); do echo "== [fake hardware keyboard] $$s"; SIM_HW=1 $(BUILD)/cardputer_sim $$s $(BUILD)/shots_hw; done

README_DIR := ../../docs/images
readme-images: $(BUILD)/cardputer_sim $(TILES_STAMP)
	@mkdir -p $(BUILD)/shots $(README_DIR)
	@set -e; for s in scenarios/readme_*.txt; do echo "== $$s"; $(BUILD)/cardputer_sim $$s $(BUILD)/shots; done
	cp $(BUILD)/shots/readme_*.png $(README_DIR)/
	python3 ../../tools/png_compress.py $(README_DIR)/readme_*.png

clean:
	rm -rf $(BUILD)/core $(BUILD)/test_core $(BUILD)/sim $(BUILD)/wire $(BUILD)/cardputer_sim $(BUILD)/unit_tests $(BUILD)/shots

distclean:
	rm -rf $(BUILD)

.PHONY: all run test wire dbtest readme-images clean distclean
