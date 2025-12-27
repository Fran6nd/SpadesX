# SpadesX Makefile
# Simple wrapper around CMake for convenience

BUILD_DIR := build
CMAKE := cmake
MAKE := make

.PHONY: all server plugins clean run help

# Default target - build everything
all: server plugins

# Configure and build the server
server:
	@echo "Building SpadesX server..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. && $(MAKE)

# Build just the plugins
plugins:
	@echo "Building plugins..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. && $(MAKE) example_gamemode

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@rm -f plugins/example_gamemode.dylib plugins/example_gamemode.so plugins/example_gamemode.dll

# Run the server (from build directory)
run: all
	@echo "Starting SpadesX server..."
	@cd $(BUILD_DIR) && ./SpadesX

# Display help
help:
	@echo "SpadesX Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build server and plugins (default)"
	@echo "  server    - Build only the server"
	@echo "  plugins   - Build only the plugins"
	@echo "  clean     - Remove all build artifacts"
	@echo "  run       - Build and run the server"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make          # Build everything"
	@echo "  make plugins  # Build only plugins"
	@echo "  make run      # Build and run server"
