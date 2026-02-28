# Compiler
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I./src -MMD -MP
# Directories
SRC_DIR = src
BUILD_DIR = build
# Files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)
# Output
TARGET = app
# Default target
all: $(TARGET)
# Link
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)
# Compile
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGET)
# Include dependency files
-include $(DEPS)
