# Host simulator for the gas fork (GPL-3.0)
#   make test      build and run all scenarios, results in out/
SRC_PM = ../../src/pitmaster
CXX ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare -Wno-delete-non-virtual-dtor -Wno-maybe-uninitialized
INCLUDES = -Istubs -I$(SRC_PM) -Ithird_party -DARDUINO=100
SOURCES = sim_main.cpp plant.cpp $(SRC_PM)/Pitmaster.cpp $(SRC_PM)/PidCore.cpp \
          $(SRC_PM)/AutoTuneRelay.cpp $(SRC_PM)/FlameOutDetector.cpp

all: gas_sim

gas_sim: $(SOURCES) $(wildcard stubs/*.h stubs/*/*.h *.h $(SRC_PM)/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SOURCES)

test: gas_sim
	mkdir -p out
	./gas_sim out

clean:
	rm -rf gas_sim out

.PHONY: all test clean
