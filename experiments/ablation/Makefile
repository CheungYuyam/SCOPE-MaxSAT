CXX ?= g++
WRAPPER_CXXFLAGS ?= -O3 -DNDEBUG -DNBUILD -DNUNLOCKED -std=c++11
CORE_CXXFLAGS ?= -O3
LDFLAGS ?= -static

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
PLATFORM_LIBS := -lpsapi
CREATE_BUILD_DIR := if not exist build mkdir build
CREATE_CADICAL_DIR := if not exist build\cadical mkdir build\cadical
CLEAN_COMMAND := if exist build rmdir /s /q build & if exist nuwls.exe del /q nuwls.exe & if exist nuwls-core.exe del /q nuwls-core.exe & if exist nuwls-quality-core.exe del /q nuwls-quality-core.exe & if exist scope-a1.exe del /q scope-a1.exe & if exist scope-a2.exe del /q scope-a2.exe & if exist scope-a3.exe del /q scope-a3.exe & if exist scope-a4.exe del /q scope-a4.exe & if exist scope-a5.exe del /q scope-a5.exe & if exist scope-a6.exe del /q scope-a6.exe & if exist scope-a7.exe del /q scope-a7.exe & if exist scope-y10.exe del /q scope-y10.exe & if exist scope-y01.exe del /q scope-y01.exe & if exist scope-y11.exe del /q scope-y11.exe & if exist wcnf-verify.exe del /q wcnf-verify.exe
else
EXEEXT :=
PLATFORM_LIBS :=
CREATE_BUILD_DIR := mkdir -p build
CREATE_CADICAL_DIR := mkdir -p build/cadical
CLEAN_COMMAND := rm -rf build nuwls nuwls-core nuwls-quality-core scope-a1 scope-a2 scope-a3 scope-a4 scope-a5 scope-a6 scope-a7 scope-y10 scope-y01 scope-y11 wcnf-verify
endif

CADICAL_DIR := third_party/cadical/src
CADICAL_SRC := $(filter-out $(CADICAL_DIR)/cadical.cpp $(CADICAL_DIR)/mobical.cpp,$(wildcard $(CADICAL_DIR)/*.cpp))
CADICAL_OBJ := $(patsubst $(CADICAL_DIR)/%.cpp,build/cadical/%.o,$(CADICAL_SRC))
WRAPPER_OBJ := build/pms.o build/hard_sat_fallback.o build/subprocess_runner.o
CORE_HEADERS := basis_pms.h build.h deci.h heuristic.h pms.h
ABLATION_TARGETS := scope-a1$(EXEEXT) scope-a2$(EXEEXT) scope-a3$(EXEEXT) \
	scope-a4$(EXEEXT) scope-a5$(EXEEXT) scope-a6$(EXEEXT) scope-a7$(EXEEXT) \
	scope-y10$(EXEEXT) scope-y01$(EXEEXT) scope-y11$(EXEEXT)

.PHONY: all clean

all: nuwls$(EXEEXT) nuwls-core$(EXEEXT) nuwls-quality-core$(EXEEXT) \
	$(ABLATION_TARGETS) wcnf-verify$(EXEEXT)

nuwls$(EXEEXT): $(WRAPPER_OBJ) $(CADICAL_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS) $(PLATFORM_LIBS)

nuwls-core$(EXEEXT): nuwls_core.cpp $(CORE_HEADERS)
	$(CXX) nuwls_core.cpp $(CORE_CXXFLAGS) -o $@ $(LDFLAGS)

nuwls-quality-core$(EXEEXT): nuwls_core.cpp $(CORE_HEADERS)
	$(CXX) nuwls_core.cpp $(CORE_CXXFLAGS) -DNUWLS_QUALITY_SEARCH -o $@ $(LDFLAGS)

wcnf-verify$(EXEEXT): wcnf_verify.cpp
	$(CXX) wcnf_verify.cpp $(WRAPPER_CXXFLAGS) -o $@ $(LDFLAGS)

define SCOPE_VARIANT_template
scope-$(1)$(EXEEXT): build/ablation_$(1).o build/hard_sat_fallback.o build/subprocess_runner.o $(CADICAL_OBJ)
	$$(CXX) $$^ -o $$@ $$(LDFLAGS) $$(PLATFORM_LIBS)

build/ablation_$(1).o: ablation_pms.cpp hard_sat_fallback.h subprocess_runner.h | build
	$$(CXX) $$(WRAPPER_CXXFLAGS) -I$$(CADICAL_DIR) -DSCOPE_ABLATION_VARIANT=$(2) -c $$< -o $$@
endef

$(eval $(call SCOPE_VARIANT_template,a1,1))
$(eval $(call SCOPE_VARIANT_template,a2,2))
$(eval $(call SCOPE_VARIANT_template,a3,3))
$(eval $(call SCOPE_VARIANT_template,a4,4))
$(eval $(call SCOPE_VARIANT_template,a5,5))
$(eval $(call SCOPE_VARIANT_template,a6,6))
$(eval $(call SCOPE_VARIANT_template,a7,7))
$(eval $(call SCOPE_VARIANT_template,y10,10))
$(eval $(call SCOPE_VARIANT_template,y01,11))
$(eval $(call SCOPE_VARIANT_template,y11,12))

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
