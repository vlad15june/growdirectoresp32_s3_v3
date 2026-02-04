# Install just:
#   MacOS: brew install just
#   Linux: curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin
#   Windows: choco install just
#
# Usage:
#   just            - Show available commands
#   just build      - Build the firmware for esp32-s3-devkitm-1 using VERSION file
#   just test       - Run native tests using VERSION file
#   just clean      - Clean the build artifacts
#   just monitor    - Start serial monitor
#   just all        - Build the firmware

version := `cat VERSION`

# Show available commands
default:
    @just --list

# Build the firmware for esp32-s3-devkitm-1 using VERSION file
build:
    PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='{{version}}'" pio run -e esp32-s3-devkitm-1

# Run native tests using VERSION file
test:
    PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='{{version}}'" pio test -e native -vvv

# Clean the build artifacts
clean:
    pio run --target clean

# Start serial monitor
monitor:
    pio device monitor -e esp32-s3-devkitm-1

# Build the firmware
alias all := build
