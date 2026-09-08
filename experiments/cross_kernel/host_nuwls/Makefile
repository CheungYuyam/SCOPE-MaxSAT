CXX ?= g++
CXXFLAGS ?= -O3 -std=c++11
LDFLAGS ?= -static

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
CLEAN_COMMAND := if exist official-nuwls.exe del /q official-nuwls.exe
else
EXEEXT :=
CLEAN_COMMAND := rm -f official-nuwls
endif

.PHONY: all clean

all: official-nuwls$(EXEEXT)

official-nuwls$(EXEEXT): basis_pms.h build.h pms.h heuristic.h deci.h pms.cpp
	$(CXX) pms.cpp $(CXXFLAGS) -o $@ $(LDFLAGS)

clean:
	@$(CLEAN_COMMAND)
