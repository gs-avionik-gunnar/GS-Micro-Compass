SKETCH := ../GS-Micro-Compass
CXXFLAGS := -std=gnu++11 -O1 -g -Wall -Wextra -I$(SKETCH)
CORE := $(SKETCH)/Compass.cpp $(SKETCH)/SettingsCore.cpp $(SKETCH)/Gfx.cpp $(SKETCH)/DialUi.cpp $(SKETCH)/DialFont.cpp

all: run

test_core: test_main.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined $^ -o $@

run: test_core
	./test_core

dial_render: dial_render.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined $^ -o $@

# Compile-only check of the Arduino glue against minimal stub headers (no ESP32 core needed).
syntax:
	for f in Settings App Mag WebUi DialHw; do \
	  $(CXX) -std=gnu++11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Istubs -I$(SKETCH) $(SKETCH)/$$f.cpp || exit 1; \
	done
	$(CXX) -std=gnu++11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DCONFIG_IDF_TARGET_ESP32S3 -Istubs -I$(SKETCH) $(SKETCH)/DialHw.cpp
	$(CXX) -std=gnu++11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DCONFIG_IDF_TARGET_ESP32S3 -DESP_ARDUINO_VERSION_MAJOR=3 -Istubs -I$(SKETCH) $(SKETCH)/DialHw.cpp
	$(CXX) -std=gnu++11 -fsyntax-only -Wall -Wno-unused-parameter -Istubs -I$(SKETCH) -x c++ $(SKETCH)/GS-Micro-Compass.ino

clean:
	rm -f test_core dial_render *.ppm *.png
