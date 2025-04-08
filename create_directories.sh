#!/bin/bash

# Navigate to project root
cd "/Users/arsenarutiunian/Projects/GrowDirector/esp32/PlatformIO IDE/growdirectoresp32_S3_v3"

# Navigate to existing src directory
cd src

# Create the Core directory and its subdirectories
mkdir -p Core/
mkdir -p Core/Domain/
mkdir -p Core/Domain/Entities
mkdir -p Core/Domain/Interfaces
mkdir -p Core/Domain/UseCases

# Create the Infrastructure directory and its subdirectories
mkdir -p Infrastructure
mkdir -p Infrastructure/Logging
mkdir -p Infrastructure/Persistence
mkdir -p Infrastructure/Communication
mkdir -p Infrastructure/Sensors
mkdir -p Infrastructure/Devices
mkdir -p Infrastructure/Scheduling
mkdir -p Infrastructure/Configuration

# Create the Application directory and its subdirectories
mkdir -p Application
mkdir -p Application/Services
mkdir -p Application/Commands
mkdir -p Application/DTOs

# Create the Presentation directory and its subdirectories
mkdir -p Presentation
mkdir -p Presentation/Web
mkdir -p Presentation/MQTT
mkdir -p Presentation/Controllers

echo "Directory structure created successfully!"
