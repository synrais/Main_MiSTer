CXX ?= arm-none-linux-gnueabihf-g++
CXXFLAGS ?= -O2 -Wall
TARGET := input_monitor

all: $(TARGET)

$(TARGET): input_monitor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
