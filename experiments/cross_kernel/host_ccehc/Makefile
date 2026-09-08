CXX ?= g++
WRAPPER_CXXFLAGS ?= -O3 -DNDEBUG -DNBUILD -DNUNLOCKED -std=c++11
CCEHC_CXXFLAGS ?= -O3 -std=c++11
LDFLAGS ?= -static

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
PLATFORM_LIBS := -lpsapi
CREATE_BUILD_DIR := if not exist build mkdir build
CREATE_CADICAL_DIR := if not exist build\cadical mkdir build\cadical
CLEAN_COMMAND := if exist build rmdir /s /q build & if exist ccehc-host.exe del /q ccehc-host.exe & if exist ccehc-seeded.exe del /q ccehc-seeded.exe
else
EXEEXT :=
PLATFORM_LIBS :=
CREATE_BUILD_DIR := mkdir -p build
CREATE_CADICAL_DIR := mkdir -p build/cadical
CLEAN_COMMAND := rm -rf build ccehc-host ccehc-seeded
endif

CADICAL_DIR := third_party/cadical/src
CADICAL_SRC := $(filter-out $(CADICAL_DIR)/cadical.cpp $(CADICAL_DIR)/mobical.cpp,$(wildcard $(CADICAL_DIR)/*.cpp))
CADICAL_OBJ := $(patsubst $(CADICAL_DIR)/%.cpp,build/cadical/%.o,$(CADICAL_SRC))
WRAPPER_OBJ := build/pms.o build/hard_sat_fallback.o build/subprocess_runner.o
CCEHC_DIR := third_party/CCEHC
CCEHC_HEADERS := $(CCEHC_DIR)/basis_wpms.h $(CCEHC_DIR)/wpms.h $(CCEHC_DIR)/paws.h

.PHONY: all clean

all: ccehc-host$(EXEEXT) ccehc-seeded$(EXEEXT)

ccehc-host$(EXEEXT): ccehc_host.cpp build/subprocess_runner.o
	$(CXX) $(WRAPPER_CXXFLAGS) $^ -o $@ $(LDFLAGS) $(PLATFORM_LIBS)

ccehc-seeded$(EXEEXT): $(CCEHC_DIR)/wpms.cpp $(CCEHC_HEADERS)
	$(CXX) $(CCEHC_CXXFLAGS) $(CCEHC_DIR)/wpms.cpp -o $@ $(LDFLAGS)

build:
	@$(CREATE_BUILD_DIR)

build/cadical: | build
	@$(CREATE_CADICAL_DIR)

build/pms.o: pms.cpp hard_sat_fallback.h subprocess_runner.h | build
	$(CXX) $(WRAPPER_CXXFLAGS) -I$(CADICAL_DIR) -c $< -o $@

build/hard_sat_fallback.o: hard_sat_fallback.cpp hard_sat_fallback.h $(CADICAL_DIR)/cadical.hpp | build
	$(CXX) $(WRAPPER_CXXFLAGS) -I$(CADICAL_DIR) -c $< -o $@

build/subprocess_runner.o: subprocess_runner.cpp subprocess_runner.h | build
	$(CXX) $(WRAPPER_CXXFLAGS) -c $< -o $@

build/cadical/%.o: $(CADICAL_DIR)/%.cpp $(CADICAL_DIR)/*.hpp | build/cadical
	$(CXX) $(WRAPPER_CXXFLAGS) -I$(CADICAL_DIR) -c $< -o $@

clean:
	@$(CLEAN_COMMAND)
