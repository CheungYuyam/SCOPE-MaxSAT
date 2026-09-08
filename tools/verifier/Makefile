CXX ?= g++
CXXFLAGS ?= -O2 -std=c++11
LDFLAGS ?= -static

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
CLEAN_COMMAND := if exist wcnf-verify.exe del /q wcnf-verify.exe
else
EXEEXT :=
CLEAN_COMMAND := rm -f wcnf-verify
endif

.PHONY: all clean

all: wcnf-verify$(EXEEXT)

wcnf-verify$(EXEEXT): wcnf_verify.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	@$(CLEAN_COMMAND)
