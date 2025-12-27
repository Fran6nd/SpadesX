# SpadesX Makefile
# Simple wrapper around CMake for convenience

BUILD_DIR := build
CMAKE := cmake
MAKE := make

.PHONY: all server plugins template clean run help init-template template-clean

# Default target - build everything
all: server plugins template

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

# Build plugin template
template: init-template
	@echo "Building plugin template..."
	@cd plugin-template && $(MAKE)
	@echo "Plugin template built successfully"

# Clean build artifacts
clean: template-clean
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@rm -f plugins/example_gamemode.dylib plugins/example_gamemode.so plugins/example_gamemode.dll

# Run the server (from build directory)
run: all
	@echo "Starting SpadesX server..."
	@cd $(BUILD_DIR) && ./SpadesX

# Initialize plugin template submodule
init-template:
	@echo "Initializing plugin template submodule..."
	@git submodule update --init plugin-template
	@echo "Plugin template ready at plugin-template/"

# Clean plugin template build artifacts
template-clean:
	@echo "Cleaning plugin template build artifacts..."
	@if [ -d plugin-template/build ]; then rm -rf plugin-template/build; fi
	@echo "Plugin template cleaned"

# Display help
help:
	@echo "SpadesX Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build server, plugins, and template (default)"
	@echo "  server         - Build only the server"
	@echo "  plugins        - Build only the plugins"
	@echo "  template       - Build plugin template"
	@echo "  clean          - Remove all build artifacts (including template)"
	@echo "  run            - Build and run the server"
	@echo "  init-template  - Initialize plugin template submodule"
	@echo "  template-clean - Clean plugin template build artifacts"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make               # Build everything including template"
	@echo "  make server        # Build only the server"
	@echo "  make template      # Build plugin template"
	@echo "  make run           # Build and run server"
