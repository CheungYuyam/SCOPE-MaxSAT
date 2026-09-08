CXX ?= g++
WRAPPER_CXXFLAGS ?= -O3 -DNDEBUG -DNBUILD -DNUNLOCKED -std=c++11
CORE_CXXFLAGS ?= -O3
LDFLAGS ?= -static

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
PLATFORM_LIBS := -lpsapi
CREATE_BUILD_DIR := if not exist build mkdir build
CREATE_CADICAL_DIR := if not exist build\cadical mkdir build\cadical
CLEAN_COMMAND := if exist build rmdir /s /q build & if exist scope-maxsat.exe del /q scope-maxsat.exe & if exist nuwls-core.exe del /q nuwls-core.exe & if exist nuwls-quality-core.exe del /q nuwls-quality-core.exe
else
EXEEXT :=
PLATFORM_LIBS :=
CREATE_BUILD_DIR := mkdir -p build
CREATE_CADICAL_DIR := mkdir -p build/cadical
CLEAN_COMMAND := rm -rf build scope-maxsat nuwls-core nuwls-quality-core
endif

CADICAL_DIR := third_party/cadical/src
CADICAL_SRC := $(filter-out $(CADICAL_DIR)/cadical.cpp $(CADICAL_DIR)/mobical.cpp,$(wildcard $(CADICAL_DIR)/*.cpp))
CADICAL_OBJ := $(patsubst $(CADICAL_DIR)/%.cpp,build/cadical/%.o,$(CADICAL_SRC))
WRAPPER_OBJ := build/pms.o build/hard_sat_fallback.o build/subprocess_runner.o
CORE_HEADERS := basis_pms.h build.h deci.h heuristic.h pms.h

.PHONY: all clean

all: scope-maxsat$(EXEEXT) nuwls-core$(EXEEXT) nuwls-quality-core$(EXEEXT)

scope-maxsat$(EXEEXT): $(WRAPPER_OBJ) $(CADICAL_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS) $(PLATFORM_LIBS)

nuwls-core$(EXEEXT): nuwls_core.cpp $(CORE_HEADERS)
	$(CXX) nuwls_core.cpp $(CORE_CXXFLAGS) -o $@ $(LDFLAGS)

nuwls-quality-core$(EXEEXT): nuwls_core.cpp $(CORE_HEADERS)
	$(CXX) nuwls_core.cpp $(CORE_CXXFLAGS) -DNUWLS_QUALITY_SEARCH -o $@ $(LDFLAGS)

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
