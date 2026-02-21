# Sensor Models

## Overview

This page contains the current list of supported sensor models in the GrowDirector system.

## Sensor Models Table

| Display Name | Model Name | Protocol | Baud Rate | Modbus Address | Supported | Can Scan | Sensor Types |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RY-WS301 Environmental Sensor | ry_ws301 | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | Air Temperature (°C), Air Humidity (%) |
| MP-508C Soil Temperature and Humidity | mp-508c | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | Soil Moisture (%), Soil Temperature (°C) |
| RY-CWYW/485 Pressure Water Level Gauge | ry-cwyw485 | Modbus | 9600 | 1 | ✓ | ✓ | Water Level (m), Water Temperature (°C) |
| RY-VS09 CO2 Sensor | ry-vs09 | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | CO2 (ppm) |
| RY-CGH Photosynthetically Active Radiation Sensor | ry-cgh | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | PAR (μmol/m²/s) |
| RY-G/W Illuminance Sensor | ry-gw | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | Illuminance (Lux) |
| XF600A Compact Weather Station | xf600a | Modbus | 9600 | 255 (0xFF) | ✓ | ✓ | Air Temperature, Humidity, Pressure, Wind Speed/Direction, Rainfall |
| RY-WS12S Component Temperature Sensor | ry-ws12s | Modbus | 9600 | 1 | ✓ | ✓ | Surface/Component Temperature (°C) |
| CC-FL-120W Water Flow Meter | cc-fl-120w | Modbus | 9600 | 1 | ✓ | ✓ | Water Flow (m³), Valve Control |

---

## Detailed Sensor Information

### RY-WS301 Environmental Sensor

**Description**: Pen-type temperature and humidity sensor designed for indoor and outdoor meteorological environment monitoring.

* **UID**: 4719a986-a300-48d5-bd1f-e34a1329798a
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 9-24V
* **Working Current**: < 20mA (RS485)
* **Power Consumption**: < 0.2W (RS485)
* **Working Temperature**: -40°C to 85°C
* **Protection Level**: IP65
* **Cable Length**: 3m (customizable)
* **Response Time**: < 15s

**Sensor Types**:

| Parameter | Range | Accuracy | Resolution | Optimal Range |
| --- | --- | --- | --- | --- |
| Air Temperature | -40°C to 85°C | ±0.3°C (@ 25°C, typical) | 0.01°C | 15-35°C |
| Air Humidity | 0-100% RH | ±3% RH (@ 25°C, 10%-80% RH, typical) | 0.01% RH | 45-60% |

**Modbus Registers** (Function Code 03):

| Address | Parameter | Formula |
| --- | --- | --- |
| 0x0001 | Air Temperature | (value / 100) - 40 = °C |
| 0x0002 | Air Humidity | value / 100 = %RH |

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Yellow | RS485-A |
| Blue | RS485-B |

---

### MP-508C Soil Temperature and Humidity

**Description**: Integrated soil moisture and temperature sensor using FDR (Frequency Domain Reflectometry) principle for volumetric soil moisture measurement.

* **UID**: 144f6c6d-d210-44bf-a9c3-23c5f26472fe
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 12-24V
* **Working Current**: 20mA (@ DC12V)
* **Power Consumption**: ≤ 0.3W
* **Working Temperature**: -20°C to 80°C
* **Protection Level**: IP68
* **Cable Length**: 3m (standard)
* **Response Time**: < 1s
* **Stabilization Time**: ~10 seconds after power on
* **Probe Material**: 316 Stainless Steel
* **Measurement Area**: Cylinder 2.5cm diameter × 6cm long surrounding central probe

**Sensor Types**:

| Parameter | Range | Accuracy | Resolution | Optimal Range |
| --- | --- | --- | --- | --- |
| Soil Moisture | 0-100% (m³/m³) | ±2% (@ 25°C, 0-50% VOL) | 0.1% | 45-65% |
| Soil Temperature | -20°C to 80°C | ±0.5°C (@ 25°C) | 0.01°C | 15-25°C |

**Modbus Registers** (Function Code 03):

| Address | Parameter | Formula |
| --- | --- | --- |
| 0x0001 | Soil Moisture | value / 10 = %VOL |
| 0x0002 | Soil Temperature | (value / 100) - 20 = °C |

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Green | RS485-A |
| Blue | RS485-B |

**Installation Notes**:
- Fully insert steel needle into soil without shaking
- Avoid hard objects and stones
- Ensure uniform soil density at measurement point
- Can be installed horizontally or vertically
- Avoid repeated plugging/unplugging at same position

---

### RY-CWYW/485 Pressure Water Level Gauge

**Description**: Pressure-based water level transmitter for measuring water level and temperature using mixed signal processing technology with digital calibration and temperature compensation.

* **UID**: 95516b14-74fe-418b-9a76-8e929ae66bd0
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 1
* **Power Supply**: DC 3.8-30V (typical 24V)
* **Power Protection**: Anti-reverse connection, over voltage protection
* **Operating Temperature**: -10°C to 80°C
* **Compensation Temperature**: 0°C to 50°C
* **Storage Temperature**: -40°C to 125°C
* **Protection Level**: IP68
* **Housing Material**: Stainless Steel 316L
* **Cable Material**: Polyurethane or PVC (optional)
* **Response Time**: 200ms
* **Overload Capability**: 2× Full Scale

**Sensor Types**:

| Parameter | Range | Accuracy | Resolution |
| --- | --- | --- | --- |
| Water Level | 0-10m to 0-200m H₂O (customizable) | ±0.05% F.S (options: 0.1%, 0.25%, 0.5% F.S) | 0.001m |
| Water Temperature | -10°C to 80°C | ±0.2°C (0-70°C) | 0.01°C |

**Temperature Drift**:
- Zero: < 0.005% F.S/°C
- Full Scale: < 0.005% F.S/°C

**Long-term Stability**: < 0.05% F.S/year

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Green | RS485-A |
| Blue | RS485-B |

---

### RY-VS09 CO2 Sensor

**Description**: NDIR (Non-Dispersive Infrared) CO2 sensor with imported Swedish probe for measuring ambient CO2 concentration.

* **UID**: e255127a-0cc7-44f9-a471-9ef013fa3cf4
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 12V
* **Working Current**: < 35mA (RS485), < 55mA (current output)
* **Power Consumption**: < 0.42W (RS485), < 0.66W (current output)
* **Working Temperature**: 0°C to 50°C
* **Working Humidity**: 0-100% RH (when powered)
* **Protection Level**: IP65
* **Preheating Time**: < 1 minute
* **Cable Length**: 1.5m (RS485: 3m)
* **Maximum Lead Distance**: RS485: 100m, Current: 200m
* **Size**: 140mm × 30mm
* **Weight**: ~200g

**Sensor Types**:

| Parameter | Range | Accuracy | Optimal Range |
| --- | --- | --- | --- |
| CO2 | 0-5000 ppm | ±70ppm ±3% of reading* | 400-1500 ppm |

*Accuracy achieved after 3 weeks of continuous operation with ABC enabled, in the range of 0-2000ppm, in general application environment.

**Modbus Registers** (Function Code 03):

| Address | Parameter | Formula |
| --- | --- | --- |
| 0x0000 | CO2 Concentration | value = ppm |

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Green | RS485-A |
| Blue | RS485-B |

---

### RY-CGH Photosynthetically Active Radiation Sensor

**Description**: PAR sensor measuring photosynthetically active radiation (400-700nm wavelength) using silicon photodetector with optical filter.

* **UID**: a8fe3ef8-8c22-49ad-b401-ad87ac52033d
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 12V
* **Working Temperature**: -40°C to 65°C
* **Working Humidity**: < 90%
* **Protection Level**: IP65
* **Cable Length**: 3m (standard)
* **Maximum Lead Distance**: RS485: 500m, Current: 200m
* **Response Time**: < 1s (99%)
* **Internal Resistance**: < 2KΩ
* **Weight**: ~220g

**Sensor Types**:

| Parameter | Range | Sensitivity | Temperature Effect | Optimal Range |
| --- | --- | --- | --- | --- |
| PAR | 0-4000 μmol/m²/s | 7-50 μV/μmol·m⁻²·s⁻¹ | Max 0.05%/°C | 200-800 μmol/m²/s |

**Spectral Range**: 400-700nm

**Cosine Calibration**: Up to 80° incident angle

**Modbus Registers** (Function Code 03):

| Address | Parameter | Formula |
| --- | --- | --- |
| 0x0000 | PAR Radiation | value = μmol/m²/s |

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Yellow | RS485-A |
| Blue | RS485-B |

---

### RY-G/W Illuminance Sensor

**Description**: Illuminance sensor using photoelectric principle with aluminum alloy housing for measuring visible light (400-700nm).

* **UID**: 500f6788-fc70-44fd-8e0f-42c00ae32fd8
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 12V/24V
* **Working Current**: < 35mA (voltage/current), < 20mA (RS485)
* **Power Consumption**: < 0.42W (voltage/current), < 0.24W (RS485)
* **Working Temperature**: -30°C to 60°C
* **Protection Level**: IP65
* **Cable Length**: 3m (standard)
* **Response Time**: 1s
* **Weight**: 220g

**Sensor Types** (available variants):

| Variant | Range | Accuracy | Formula |
| --- | --- | --- | --- |
| 0-2000 Lux | 0-2000 Lux | ±3% or 1% F.S | value = Lux |
| 0-20K Lux | 0-20,000 Lux | ±3% or 1% F.S | value = Lux |
| 0-200K Lux | 0-200,000 Lux | ±3% or 1% F.S | value / 100 = KLux |

**Spectral Range**: 400-700nm (visible light)

**Modbus Registers** (Function Code 03):

| Address | Parameter | Note |
| --- | --- | --- |
| 0x0000 | Illuminance | See variant for formula |

**Wiring Definition (Current Output)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Yellow | Current Signal Output |

---

### XF600A Compact Weather Station

**Description**: Integrated six-parameter weather station measuring ambient temperature, relative humidity, atmospheric pressure, wind speed, wind direction, and piezoelectric rainfall in one compact structure.

* **UID**: *To be assigned*
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 255 (0xFF)
* **Power Supply**: DC 9-24V
* **Operating Temperature**: -40°C to 80°C
* **Protection Level**: IP65
* **Cable Length**: 3m (standard), 10m (optional)
* **Output Options**: RS485 (standard), SDI-12 (optional)
* **Maximum Query Frequency**: 1/second (passive mode), 1/minute (active mode)

**Sensor Types**:

| Parameter | Range | Accuracy | Resolution | Sampling Freq |
| --- | --- | --- | --- | --- |
| Air Temperature | -40°C to 60°C | ±0.3°C (@ 25°C) | 0.01°C | 1 Hz |
| Relative Humidity | 0-100% RH | ±3% RH (10-80% RH, no condensation) | 0.01% RH | 1 Hz |
| Atmospheric Pressure | 500-1100 hPa | ±0.5 hPa (@ 25°C, 950-1100 hPa) | 0.1 hPa | 1 Hz |
| Wind Speed | 0-60 m/s | ±(0.3+0.03V) m/s (V≤30m/s) | 0.01 m/s | 4 Hz |
| Wind Direction | 0-359.9° | ±3° (wind speed < 10m/s) | 0.1° | 4 Hz |
| Rainfall | 0-200 mm/h | Piezoelectric type | 0.2 mm | 1 Hz |

**Modbus Registers** (Function Code 03):

| Address | Parameter | Formula |
| --- | --- | --- |
| 0x0009 | Air Temperature | (value / 100) - 40 = °C |
| 0x000A | Air Humidity | value / 100 = %RH |
| 0x000B | Atmospheric Pressure | value / 10 = hPa |
| 0x000C | Wind Speed | value / 100 = m/s |
| 0x000D | Wind Direction | value / 10 = ° (North = 0°) |
| 0x000E | 10-min Rainfall | value / 10 = mm |

**Features**:
- Ultrasonic wind speed/direction measurement (no moving parts)
- Three-layer outdoor radiation shield for temperature/humidity
- Special heat insulation treatment
- Optional: Electronic compass, GPS positioning, heating function, 4G connectivity

**Wiring Definition (RS485)**:

| Wire Color | Function |
| --- | --- |
| Red | V+ (Power Supply) |
| Black | GND |
| Yellow | RS485-A |
| Blue | RS485-B |

---

### RY-WS12S Component Temperature Sensor

**Description**: High-precision component/surface temperature sensor with special disc design for contact temperature monitoring. Features anti-overvoltage, anti-overcurrent, and anti-reverse connection protection circuits.

* **UID**: *To be assigned*
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600 (configurable: 2400, 4800, 9600, 19200)
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 1 (range: 1-254)
* **Power Supply**: DC 7-15V (24V optional)
* **Working Current**: < 15mA (@ 12V)
* **Collection Box Size**: 60 × 35 × 25 mm
* **Probe Specifications**: Stainless Steel Φ6 × 30mm with 1m wire
* **Cable Length**: 15m (transmitter line)
* **Response Timeout**: 500ms

**Sensor Types**:

| Parameter | Range | Accuracy | Resolution |
| --- | --- | --- | --- |
| Component Temperature | -50°C to +120°C | ≤ ±0.3°C | 0.01°C |

**Modbus Registers**:

| Address | Contents | Operation | Formula |
| --- | --- | --- | --- |
| 0x0000 | Temperature Value | Read (03) | (value / 100) - 50 = °C |
| 0x0100 | Device Address | Read/Write (03/06) | Range 1-254, default 1 |
| 0x0101 | Communication Parameters | Read/Write (03/06) | 1=2400, 2=4800, 3=9600, 4=19200 |
| 0x0102 | Temperature Correction | Read/Write (03/06) | 0-200 → -10.0 to +10.0°C (100=no correction) |

**Communication Examples**:

Read Temperature (Address 01):
```
Request:  01 03 00 00 00 01 84 0A
Response: 01 03 02 1F 59 70 4E
Calculation: 0x1F59 = 8025, Temperature = 8025/100 - 50 = 30.25°C
```

Read Device Address (Broadcast):
```
Request:  FF 03 01 00 00 01 90 28
Response: FF 03 02 00 XX CRC1 CRC2 (XX = device address)
```

Restore Factory Settings:
```
Request:  FF 06 10 00 00 AA 18 AB
Response: FF 06 10 00 00 AA 18 AB
(Restores: Address=1, Baud=9600, Correction=100)
```

**Features**:
- Special disc design for easy surface contact
- Anti-overvoltage, anti-overcurrent, anti-reverse protection
- Lightweight, compact, waterproof design
- Supports remote soft reset
- OEM compatible

---

### CC-FL-120W Water Flow Meter

**Description**: Dry-type photoelectric direct-reading water meter using photoelectric technology to read digit wheel data with zero electromechanical conversion error. Low power design - only powered when reading or operating valve.

* **UID**: *To be assigned*
* **Protocol**: Modbus RTU
* **Baud Rate**: 9600 (configurable: 1200, 2400, 4800, 9600)
* **Data Bits**: 8
* **Stop Bits**: 1
* **Parity**: None
* **Default Modbus Address**: 1 (range: 1-247)
* **Power Supply**: DC 12-24V (RS485 interface)
* **Working Current**: ≤ 5.0mA (meter reading), ≤ 1.0mA (static) for RS485
* **Operating Temperature**: 0.1°C to +45°C (water), 0-95% RH
* **Storage Temperature**: -25°C to +65°C
* **Maximum Water Pressure**: 1 MPa
* **Maximum Pressure Loss**: 0.063 MPa
* **Maximum Communication Distance**: 300m
* **Protection Level**: IP68
* **Accuracy Class**: Level 2 (GB/T 778.1~3-2007)

**Available Models**:

| Model | Nominal Diameter | Overload Flow Q4 | Normal Flow Q3 | Boundary Flow Q2 | Minimum Flow Q1 |
| --- | --- | --- | --- | --- | --- |
| CC-FL-15 | DN15 | 3.125 m³/h | 2.5 m³/h | 0.050 m³/h | 0.030 m³/h |
| CC-FL-20 | DN20 | 5.00 m³/h | 4.0 m³/h | 0.080 m³/h | 0.050 m³/h |
| CC-FL-25 | DN25 | 7.875 m³/h | 6.3 m³/h | 0.128 m³/h | 0.080 m³/h |
| CC-FL-32 | DN32 | 12.5 m³/h | 10.0 m³/h | 0.20 m³/h | 0.125 m³/h |
| CC-FL-40 | DN40 | 20 m³/h | 16.0 m³/h | 0.320 m³/h | 0.200 m³/h |

**Maximum Permissible Error**:
- Low zone (Q1 to Q2): ±5%
- High zone (Q2 to Q4): ±2%

**Sensor Types**:

| Parameter | Range | Accuracy | Unit |
| --- | --- | --- | --- |
| Water Consumption | 0-99999.99 | ±2% (high zone) | m³ |
| Valve Status | Open/Closed | - | Boolean |

**Modbus Registers**:

| Address | Contents | Operation | Data Format |
| --- | --- | --- | --- |
| 0x0000-0x0001 | Total Water Consumption | Read (03/04) | 4 bytes, value × 0.01 = m³ |
| 0x0015 (high) | Communication Address | Read/Write (03/10) | 1-247 |
| 0x0015 (low) | Communication Baud Rate | Read/Write (03/10) | 01=9600, 02=4800, 03=2400, 04=1200 |

**Function Codes**:

| Code | Function | Description |
| --- | --- | --- |
| 03H/04H | Read Data Register | Read water consumption and parameters |
| 10H | Write Multiple Registers | Set address, baud rate |
| 05H | Valve Control | Open (FF) / Close (00) valve |
| 01H | Read Valve Status | Get current valve state |

**Communication Examples**:

Read Water Consumption (Address 01):
```
Request:  01 03 00 00 00 02 C4 0B
Response: 01 03 04 00 12 D6 87 XX XX
Calculation: 0x0012D687 = 1234567, Water = 1234567 × 0.01 = 12345.67 m³
```

Open Valve:
```
Request:  01 05 00 01 00 FF DC 4A
Response: 01 05 00 01 00 FF DC 4A
```

Close Valve:
```
Request:  01 05 00 01 00 00 9C 0A
Response: 01 05 00 01 00 00 9C 0A
```

Read Valve Status:
```
Request:  01 01 00 01 00 01 AC 0A
Response: 01 01 00 01 00 FF/00 XX XX (FF=open, 00=closed)
```

**Wiring Definition (RS485)**:

| Wire | Function |
| --- | --- |
| Power (+) | DC 12-24V |
| GND | Ground |
| RS485+ | Data A |
| RS485- | Data B |

**Features**:
- Zero reading error compared to pulse meters
- Low power consumption design
- Rotary vane metering structure
- High communication reliability with advanced encoding
- ESD protection > 10kV
- Short-circuit and open-circuit protection
- Compatible with DL/T645, CJ/T188 protocols

**Installation Notes**:
- Install horizontally with numbers facing upward
- Arrow must point in water flow direction
- Flush pipes before installation to remove debris
- Position faucet higher than water meter
- Avoid direct sunlight, freezing, and flooding
- Install valves before and after for maintenance

---

## Common Modbus Communication Settings

All sensors use the following default communication parameters:

| Parameter | Value |
| --- | --- |
| Baud Rate | 9600 |
| Data Bits | 8 |
| Stop Bits | 1 |
| Parity | None |
| CRC | CRC16 (Low byte first, High byte last) |
| Timeout | 200ms (no response = failure) |

### Common Modbus Commands

**Read Data** (Function Code 03):
```
[Address] 03 [Start Register High] [Start Register Low] [Num Registers High] [Num Registers Low] [CRC Low] [CRC High]
```

**Write Single Register** (Function Code 06):
```
[Address] 06 [Register High] [Register Low] [Value High] [Value Low] [CRC Low] [CRC High]
```

**Write Multiple Registers** (Function Code 10):
```
[Address] 10 [Start Register High] [Start Register Low] [Num Registers High] [Num Registers Low] [Byte Count] [Data...] [CRC Low] [CRC High]
```

**Read Station Number** (broadcast address 0x00):
```
00 03 00 01 00 01 D4 1B
```

---

## Calibration Information

| Sensor | Calibratable | Calibration Validity | Notes |
| --- | --- | --- | --- |
| RY-WS301 | No | - | Factory calibrated |
| MP-508C | Yes | 90 days | Soil-specific calibration recommended |
| RY-CWYW/485 | Yes | - | Factory calibrated with digital compensation |
| RY-VS09 | Yes (ABC) | Continuous | Automatic Baseline Correction after 3 weeks |
| RY-CGH | Yes | 180 days | Light source calibration |
| RY-G/W | No | - | Factory calibrated |
| XF600A | No | - | Factory calibrated |
| RY-WS12S | Yes | - | User adjustable ±10°C correction via register 0x0102 |
| CC-FL-120W | No | - | Factory calibrated, Level 2 accuracy class |

---

## Datasheets

Original manufacturer documentation available at:
https://drive.google.com/drive/folders/19vNVKzNifnNdo08kJ5F2N9a7B_eOJm6i?usp=drive_link