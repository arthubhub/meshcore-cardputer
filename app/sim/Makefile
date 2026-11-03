# Cardputer UI simulator - plain make + g++, no PlatformIO, no SDL needed.
#   make          build build/cardputer_sim (downloads LovyanGFX once)
#   make run      run every scenario in scenarios/, screenshots in build/shots/
#   make readme-images   screenshots of the README (scenarios/readme_*.txt) -> ../../docs/images/
#   make test     unit tests of the drivers against fake chips (tests/, fakes/), no LovyanGFX needed
#   make clean
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
             $(patsubst ../core/%.cpp,$(BUILD)/test_core/%.o,$(wildcard ../core/drivers/*.cpp) ../core/KeyMatrix.cpp ../core/Nmea.cpp) \
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
	rm -rf $(BUILD)/core $(BUILD)/test_core $(BUILD)/sim $(BUILD)/cardputer_sim $(BUILD)/unit_tests $(BUILD)/shots

distREADME_DIR := ../../docs/images
readme-images: $(BUILD)/cardputer_sim $(TILES_STAMP)
	@mkdir -p $(BUILD)/shots $(README_DIR)
	@set -e; for s in scenarios/readme_*.txt; do echo "== $$s"; $(BUILD)/cardputer_sim $$s $(BUILD)/shots; done
	cp $(BUILD)/shots/readme_*.png $(README_DIR)/
	python3 ../../tools/png_compress.py $(README_DIR)/readme_*.png

clean:
	rm -rf $(BUILD)

.PHONY: all run test readme-images clean distclean
