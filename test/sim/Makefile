# Host simulator for the gas fork (GPL-3.0)
#   make test      build and run all scenarios, results in out/
SRC_PM = ../../src/pitmaster
CXX ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare -Wno-delete-non-virtual-dtor -Wno-maybe-uninitialized
INCLUDES = -Istubs -I$(SRC_PM) -Ithird_party -DARDUINO=100
SOURCES = sim_main.cpp plant.cpp $(SRC_PM)/Pitmaster.cpp $(SRC_PM)/PidCore.cpp \
          $(SRC_PM)/AutoTuneRelay.cpp $(SRC_PM)/FlameOutDetector.cpp $(SRC_PM)/BiasMap.cpp

ARDUINOJSON = .deps/ArduinoJson

all: gas_sim unit_policy

# ArduinoJson 5 as used by the firmware (platformio.ini: ArduinoJson@^5.8.4)
$(ARDUINOJSON):
	git clone -q --depth 1 -b v5.13.5 https://github.com/bblanchon/ArduinoJson.git $(ARDUINOJSON)

unit_policy: unit_policy.cpp ../../src/ServerApiPolicy.h | $(ARDUINOJSON)
	$(CXX) -std=c++14 -O1 -Wall -I$(ARDUINOJSON)/src -o $@ unit_policy.cpp

gas_sim: $(SOURCES) $(wildcard stubs/*.h stubs/*/*.h *.h $(SRC_PM)/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SOURCES)

test: gas_sim unit_policy
	mkdir -p out
	./unit_policy
	./gas_sim out
	mkdir -p out/smart
	./gas_sim out/smart --smart

# fitted plants of real cooks (tools/fit_plant.py), informational: prints the
# checks and summary rows of each fitted plant, does not fail
plants: gas_sim
	@for f in plants/*.plant; do \
	  n=$$(basename $$f .plant); \
	  for mode in "" "--smart"; do \
	    o=out/plants/$$n$${mode:+-smart}; mkdir -p $$o; \
	    ./gas_sim $$o --plant $$f $$mode > $$o/log.txt 2>&1; rc=$$?; \
	    echo "=== $$n $${mode:-old} (exit $$rc)"; \
	    grep -E "FAIL|ok: +$$n|ALL PASSED" $$o/log.txt || true; \
	    grep -E "^\| ($$n|Szenario)" $$o/summary.md || true; \
	  done; \
	done

clean:
	rm -rf gas_sim unit_policy out

.PHONY: all test plants clean
