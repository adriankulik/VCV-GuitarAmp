# Plugin name must match the slug in plugin.json
SLUG = AdrianKulikGuitarInterface

# Rack SDK path — set RACK_DIR to wherever you installed/cloned the Rack SDK
# e.g. export RACK_DIR=~/Documents/Rack2/sdk
ifndef RACK_DIR
$(error RACK_DIR is not set. Run: export RACK_DIR=/path/to/Rack2/sdk)
endif

# Source files
SOURCES += src/plugin.cpp
SOURCES += src/GuitarFX.cpp

# Include res/ folder in the packaged plugin
DISTRIBUTABLES += res

# Flags passed to the compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Include the Rack SDK build system
include $(RACK_DIR)/plugin.mk
