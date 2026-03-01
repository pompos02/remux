# Compiler
CXX = g++
CXXFLAGS_COMMON = -Wall -Wextra -std=c++17 -I./src -MMD -MP -mavx2
LDLIBS = -lftxui-component -lftxui-dom -lftxui-screen -lpthread
# Directories
SRC_DIR = src
BUILD_DIR_DEBUG = build/debug
BUILD_DIR_RELEASE = build/release
# Files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS_DEBUG = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_DEBUG)/%.o,$(SRCS))
OBJS_RELEASE = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR_RELEASE)/%.o,$(SRCS))
DEPS_DEBUG = $(OBJS_DEBUG:.o=.d)
DEPS_RELEASE = $(OBJS_RELEASE:.o=.d)
# Output
TARGET_DEBUG = remux-debug
TARGET_RELEASE = remux
# Profiles
DEBUG_FLAGS = -O0 -g3 -DDEBUG=1
RELEASE_FLAGS = -O3 -DNDEBUG -DDEBUG=0
# Default target
all: release
debug: $(TARGET_DEBUG)
release: $(TARGET_RELEASE)
# Link
$(TARGET_DEBUG): $(OBJS_DEBUG)
	$(CXX) $(OBJS_DEBUG) -o $(TARGET_DEBUG) $(LDLIBS)
$(TARGET_RELEASE): $(OBJS_RELEASE)
	$(CXX) $(OBJS_RELEASE) -o $(TARGET_RELEASE) $(LDLIBS)
# Compile
$(BUILD_DIR_DEBUG)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR_DEBUG)
	$(CXX) $(CXXFLAGS_COMMON) $(DEBUG_FLAGS) -c $< -o $@
$(BUILD_DIR_RELEASE)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR_RELEASE)
	$(CXX) $(CXXFLAGS_COMMON) $(RELEASE_FLAGS) -c $< -o $@
# Create build directories
$(BUILD_DIR_DEBUG):
	mkdir -p $(BUILD_DIR_DEBUG)
$(BUILD_DIR_RELEASE):
	mkdir -p $(BUILD_DIR_RELEASE)
# Clean
clean:
	rm -rf build $(TARGET_DEBUG) $(TARGET_RELEASE)
