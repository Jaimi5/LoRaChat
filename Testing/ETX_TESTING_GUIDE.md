# ETX Testing Guide - Complete Workflow

**Purpose:** Step-by-step guide for testing and validating bidirectional ETX routing
**Branch:** `feature/etx-routing-loramesh`
**Date:** 2025-11-04

---

## Table of Contents

1. [Understanding the Simulation Framework](#understanding-the-simulation-framework)
2. [Selected Test Scenarios](#selected-test-scenarios)
3. [Prerequisites and Setup](#prerequisites-and-setup)
4. [Running Your First ETX Test](#running-your-first-etx-test)
5. [Analyzing Test Results](#analyzing-test-results)
6. [What to Look For](#what-to-look-for)
7. [Troubleshooting](#troubleshooting)

---

## Understanding the Simulation Framework

### How the Framework Works

The LoRaChat testing framework orchestrates **software-simulated network topologies** by injecting custom code into devices at compile time.

```
┌─────────────┐
│   main.py   │  Entry point - creates experiments
└─────┬───────┘
      │
      ▼
┌─────────────────────────┐
│ simConfiguration.py     │  Interactive config creation
│                         │  - Device port mapping
│                         │  - Simulator parameters
│                         │  - Adjacency graph definition
└─────────┬───────────────┘
          │ Creates simConfiguration.json
          ▼
┌───────────────────────────────┐
│ changeConfigurationSerial.py  │  Code injection
│                               │  - Modifies src/config.h
│                               │  - Modifies LoRaMesher BuildOptions.h
│                               │  - Injects adjacency graph into
│                               │    LoraMesher.cpp::canReceivePacket()
└──────────┬────────────────────┘
           │
           ▼
┌────────────────────────┐
│ updatePlatformio.py    │  Build, Upload, Monitor
│                        │  - Parallel builds
│                        │  - Parallel uploads
│                        │  - Serial monitoring
│                        │  - Saves logs to Monitoring/
└──────────┬─────────────┘
           │
           ▼
┌──────────────────┐
│  mqttClient.py   │  Data collection
│                  │  - Subscribes to-server/#
│                  │  - Saves simulation results
│                  │  - Tracks completion
└──────────────────┘
```

### Key Files Created During a Test

```
experiment_name/
├── simulation_1/
│   ├── simConfiguration.json        # Test configuration
│   ├── summary.json                 # Execution status
│   ├── data.json                    # MQTT results from devices
│   └── Monitoring/
│       ├── build/
│       │   └── ttgo-lora32-v1.ans  # Build logs
│       ├── upload/
│       │   └── COM9.ans            # Upload logs
│       └── monitor_COM9.ans         # ⭐ SERIAL OUTPUT - Contains all ETX logs!
└── simulation_2/
    └── ...
```

**Important:** `monitor_{port}.ans` files contain ALL serial output, including:
- ETX calculations (forward/reverse)
- Routing table updates
- Route change events
- Triggered updates
- Storm prevention logs
- Duplicate detection logs

### How Packet Loss Simulation Works

The `changeConfigurationSerial.py` script modifies the `canReceivePacket()` function in LoRaMesher at compile time:

**Original function:**
```cpp
bool LoraMesher::canReceivePacket(uint16_t source) {
    return true;  // Accept all packets in production
}
```

**Modified for testing (with loss rates):**
```cpp
bool LoraMesher::canReceivePacket(uint16_t source) {
#ifdef TESTING
    uint16_t localAddress = getLocalAddress();

    // ... node lookup code ...

    // Loss rate matrix (0.0 = perfect, 1.0 = no link)
    float const lossRateMatrix[adjacencyGraphSize][adjacencyGraphSize] = {
        { 0.0f, 0.0f, 0.2f },  // Node A: 0% to B, 20% to C
        { 0.0f, 0.0f, 0.0f },  // Node B: 0% to all
        { 0.2f, 0.0f, 0.0f },  // Node C: 20% to A, 0% to B
    };

    float lossRate = lossRateMatrix[sourceIndex][localAddressIndex];

    // Probabilistic packet drop
    float randomValue = (float)esp_random() / (float)UINT32_MAX;
    return randomValue >= lossRate;  // Keep packet if random >= loss rate
#else
    return true;
#endif
}
```

This approach:
- ✅ Simulates packet loss at the **reception** level
- ✅ Supports **asymmetric links** (A→B ≠ B→A)
- ✅ Uses **hardware RNG** for realistic randomness
- ✅ Works with **real radio timing** (no clock manipulation)
- ✅ Zero runtime overhead in production (compile-time injection)

---

## Selected Test Scenarios

Based on analysis, these 3 scenarios provide the best coverage for initial validation:

### Priority 1: Scenario 1 - "Triangle of Trust"

**Why this is MOST important:**
- Tests the **fundamental principle** of ETX routing
- Simplest topology (3 nodes) → easiest to debug
- Clear success criterion: Route via B, not direct to C

**Topology:**
```
     Node B
    /      \\
  0%        0%
  /          \\
A ----20%---- C

Path options A→C:
1. Direct: 1 hop, 20% loss → ETX ≈ 2.5 (1/0.8 + 1/0.8)
2. Via B: 2 hops, 0% loss → ETX ≈ 1.0 (1/1.0 + 1/1.0)

Expected: A chooses path via B
```

**What this validates:**
- ETX calculation is correct
- Route selection prefers low ETX over low hop count
- Routing table exchange works

---

### Priority 2: Scenario 7 - "Loop Prevention"

**Why this is CRITICAL:**
- **Safety feature** - must work or network can crash
- Tests duplicate packet detection
- Validates circular buffer and timeout mechanism

**Topology:**
```
A ←-5%-→ B
↑        ↓
5%      5%
↓        ↑
D ←-5%-→ C

Square with redundant paths
```

**What this validates:**
- Routing updates don't loop forever
- Duplicate detection cache works
- Network converges despite multiple paths
- Source address + packet ID deduplication

---

### Priority 3: Scenario 2 - "Asymmetric Alley"

**Why this validates YOUR INNOVATION:**
- Tests **bidirectional ETX** - the unique feature
- Shows forward vs reverse are calculated independently
- Demonstrates practical value of your approach

**Topology:**
```
A --[5%/40%]-- B --[5%/40%]-- C --[5%/40%]-- D
   (forward/reverse loss rates)

Forward (A→B): 5% loss → 95% ACK rate → Forward ETX ≈ 1.05
Reverse (B→A): 40% loss → 60% hello rate → Reverse ETX ≈ 1.67
Total ETX: 1.05 + 1.67 = 2.72
```

**What this validates:**
- Forward ETX based on ACK reception
- Reverse ETX based on hello reception
- Asymmetry detection and logging
- Routes account for both directions

---

## Prerequisites and Setup

### Software Requirements

```bash
# Install Python dependencies
pip install paho-mqtt platformio numpy colorama

# Verify PlatformIO installation
pio --version
```

### Hardware Requirements

- **Minimum:** 3 ESP32 devices (for Scenario 1)
- **Recommended:** 4+ devices (for all scenarios)
- **Your setup:** 10 devices (perfect!)

### MQTT Broker

Start MQTT broker (required for data collection):

```bash
# Option 1: Mosquitto (Linux/Mac)
mosquitto -v

# Option 2: Mosquitto (Windows)
# Install from https://mosquitto.org/download/
# Run from installation directory

# Option 3: Docker
docker run -it -p 1883:1883 eclipse-mosquitto
```

Verify broker is running:
```bash
# Subscribe to test topic
mosquitto_sub -h localhost -t "to-server/#" -v
```

### Check Available Devices

```bash
cd Testing
python main.py test_experiment -p
```

This prints all available serial ports and their descriptions.

---

## Running Your First ETX Test

### Step 1: Prepare Test Directory

```bash
cd Testing

# Create experiment directory
mkdir -p etx_validation_tests
```

### Step 2: Create Test Configuration

We'll use the topology generator to create pre-defined scenarios:

```bash
# Generate Scenario 1 configuration
python -c "
from topologyGenerator import TopologyGenerator
import numpy as np
import json

# Get scenario 1 topology
topology = TopologyGenerator.scenario_1_triangle_of_trust()

# Load default config template
with open('defaultConfigValues.json', 'r') as f:
    config = json.load(f)

# Set test-specific parameters
config['SimulationTimeoutMinutes'] = '20'  # 20 min test

# Simulator settings
config['Simulator']['PACKET_COUNT'] = '10'  # 10 packets per node
config['Simulator']['PACKET_DELAY'] = '120000'  # 2 minutes between packets
config['Simulator']['LOG_MESHER'] = '1'  # Enable detailed logging

# LoRaMesher settings (fast convergence for testing)
config['LoRaMesher']['HELLO_PACKETS_DELAY'] = '60'  # 1-minute hellos

# Add topology
config['LoRaMesherAdjacencyGraph'] = topology.tolist()

# Device mapping (YOU NEED TO FILL THIS)
config['DeviceMapping'] = {
    'COM9': 'ttgo-lora32-v1',   # Replace with your ports
    'COM10': 'ttgo-lora32-v1',  # Replace with your ports
    'COM11': 'ttgo-lora32-v1',  # Replace with your ports
}

# Save configuration
import os
os.makedirs('etx_validation_tests/scenario_1', exist_ok=True)
with open('etx_validation_tests/scenario_1/simConfiguration.json', 'w') as f:
    json.dump(config, f, indent=4)

print('✅ Configuration created: etx_validation_tests/scenario_1/simConfiguration.json')
"
```

**IMPORTANT:** You need to update the `DeviceMapping` section with your actual COM ports and environments!

### Step 3: Run the Test

```bash
python main.py etx_validation_tests

# When prompted:
# - Choose to overwrite (y)
# The test will automatically run all simulations in the directory
```

**What happens:**
1. ✅ Reads `simConfiguration.json`
2. ✅ Modifies source code with adjacency graph
3. ✅ Builds firmware for each environment (parallel)
4. ✅ Uploads to all devices (parallel)
5. ✅ Starts monitoring serial output
6. ✅ Waits for simulation to complete (20 minutes)
7. ✅ Collects MQTT data
8. ✅ Saves all logs

**During test, you'll see:**
```
Building...............Successfully built
Start uploading to port: COM9, env: ttgo-lora32-v1
Successfully uploaded to port: COM9
Start monitoring port: COM9
All devices started simulation
...
All devices ended simulation
All devices ended logs
Successfully closed the program
```

### Step 4: Verify Test Completed

Check for output files:
```bash
ls etx_validation_tests/scenario_1/
# Should contain:
# - simConfiguration.json
# - summary.json (execution status)
# - data.json (MQTT results)
# - Monitoring/ directory

ls etx_validation_tests/scenario_1/Monitoring/
# Should contain:
# - monitor_COM9.ans  ← All serial output here!
# - monitor_COM10.ans
# - monitor_COM11.ans
```

---

## Analyzing Test Results

### Quick Check: Did It Work?

```bash
# Check summary
cat etx_validation_tests/scenario_1/summary.json | grep -i error

# If empty/null → Test succeeded ✅
# If error present → Check error_message
```

### Extract ETX Logs

The serial logs contain all ETX-related information. Let's extract key metrics:

```bash
# Find routing table prints
grep -A 20 "Current routing table" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans

# Find ETX calculations
grep "ETX for" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans

# Find route changes
grep "Found better route" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans

# Find triggered updates
grep "Sending triggered hello packet" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans

# Find duplicate detection
grep "Duplicate packet detected" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans
```

### Example Output Analysis

**Routing Table (from Node A - address 1000):**
```
Current routing table:
0 - 2000 via 2000 ETX=1.0 (R:0.5+F:0.5) asym:1.00 Role:0
    └─ Direct: hellos=100/100 (100.0%) SNR=12
1 - 3000 via 2000 ETX=1.0 (R:0.5+F:0.5) asym:1.00 Role:0
    └─ Via: 2000
```

**Interpretation:**
- ✅ Node A routes to 3000 (Node C) **via 2000 (Node B)**
- ✅ Not routing direct despite fewer hops
- ✅ ETX values are low (~1.0) for good links
- ✅ **SUCCESS!** ETX routing is working

**Route Change Log:**
```
Found better route for 3000 via 2000: old_ETX=2.5 new_ETX=1.0
```

**Interpretation:**
- Initially tried direct route (ETX=2.5)
- Discovered route via B (ETX=1.0)
- Made correct routing decision
- ✅ **SUCCESS!** Route selection based on ETX

---

## What to Look For

### Scenario 1: Triangle of Trust

**Success Criteria:**

1. **Routing Decision**
   ```bash
   # Check Node A's routing table
   grep -A 5 "3000" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans | grep "via"
   ```
   - ✅ Should show: `via 2000` (routing through B)
   - ❌ Should NOT show: `via 3000` (direct)

2. **ETX Values**
   ```bash
   # Check ETX calculations
   grep "ETX for 3000" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans
   ```
   - ✅ Direct link (A→C): ETX ≈ 2.5 (due to 20% loss)
   - ✅ Via B (A→B→C): ETX ≈ 1.0 (two perfect links)

3. **Stability**
   ```bash
   # Count route changes
   grep "Found better route for 3000" etx_validation_tests/scenario_1/Monitoring/monitor_COM9.ans | wc -l
   ```
   - ✅ Should be 1-2 (initial convergence)
   - ❌ Should NOT be >5 (indicates flapping)

### Scenario 7: Loop Prevention

**Success Criteria:**

1. **No Routing Loops**
   ```bash
   # Check for "loop detected" or excessive processing
   grep -i "loop" etx_validation_tests/scenario_7/Monitoring/*.ans
   ```
   - ✅ Should find no loops
   - ✅ Network should converge quickly

2. **Duplicate Detection**
   ```bash
   # Check duplicate detection is working
   grep "Duplicate packet detected" etx_validation_tests/scenario_7/Monitoring/*.ans | wc -l
   ```
   - ✅ Should be >0 (duplicates caught)
   - ✅ Higher count = more redundant paths = working correctly

3. **Convergence**
   ```bash
   # Check all nodes have complete routing tables
   grep "routing table" etx_validation_tests/scenario_7/Monitoring/*.ans
   ```
   - ✅ All nodes should have routes to all other nodes
   - ✅ Convergence within 10 minutes (5 hello cycles)

### Scenario 2: Asymmetric Alley

**Success Criteria:**

1. **Bidirectional ETX Calculation**
   ```bash
   # Check forward and reverse ETX are different
   grep "Reverse ETX for" etx_validation_tests/scenario_2/Monitoring/monitor_COM9.ans
   grep "Forward ETX for" etx_validation_tests/scenario_2/Monitoring/monitor_COM9.ans
   ```
   - ✅ Forward ETX ≈ 1.05 (5% loss → 95% ACK rate)
   - ✅ Reverse ETX ≈ 1.67 (40% loss → 60% hello rate)

2. **Asymmetry Detection**
   ```bash
   # Check asymmetry ratio is logged
   grep "asym:" etx_validation_tests/scenario_2/Monitoring/monitor_COM9.ans
   ```
   - ✅ Should show asymmetry ratio ≈ 0.63 (1.05/1.67)
   - ✅ Ratio should be logged in routing table

3. **Route Selection Accounts for Bidirectional Cost**
   ```bash
   # Check total ETX (forward + reverse)
   grep "ETX=" etx_validation_tests/scenario_2/Monitoring/monitor_COM9.ans
   ```
   - ✅ Total ETX = Forward + Reverse ≈ 2.72
   - ✅ Routes selected based on total bidirectional cost

---

## Troubleshooting

### Common Issues

#### Issue 1: Build Fails

**Symptoms:**
```
[FAILED]
Build failed for env with exit code 1
```

**Solutions:**
1. Check that you're on the correct branch: `feature/etx-routing-loramesh`
2. Clean build: `pio run --target clean`
3. Check error in `Monitoring/build/{env}.ans`
4. Verify LoRaMesher library is installed

#### Issue 2: Upload Fails

**Symptoms:**
```
Upload to COM9 failed with exit code 1
```

**Solutions:**
1. Check device is connected: `python main.py test -p`
2. Close other serial terminals (conflict)
3. Try manual upload: `pio run -e ttgo-lora32-v1 --target upload --upload-port COM9`
4. Check USB cable (try a different one)

#### Issue 3: No Serial Output

**Symptoms:**
```
monitor_COM9.ans is empty or has very few lines
```

**Solutions:**
1. Check device actually booted (power LED on?)
2. Try opening serial monitor manually: `pio device monitor --port COM9`
3. Verify baudrate (should be auto-detected)
4. Check for "waiting for download" → device needs re-upload

#### Issue 4: Test Times Out

**Symptoms:**
```
Error: Timeout waiting for all devices to start simulation
```

**Solutions:**
1. Check MQTT broker is running: `mosquitto -v`
2. Check devices are on same network (if using WiFi)
3. Increase timeout in simConfiguration.json
4. Check monitor logs for crash/restart loops

#### Issue 5: ETX Logs Not Appearing

**Symptoms:**
No "ETX for" or "routing table" messages in logs

**Solutions:**
1. Check `LOG_MESHER` is set to `1` in simConfiguration.json
2. Verify code modification happened (check `.pio/libdeps/.../LoRaMesher/src/LoraMesher.cpp`)
3. Rebuild after configuration change
4. Increase test duration (ETX calculation needs time)

---

## Next Steps After Initial Tests

### If Tests Pass ✅

1. **Document Results**
   - Take screenshots of key log sections
   - Save routing tables at different time points
   - Note convergence times

2. **Run More Complex Scenarios**
   - Scenario 3: Degrading Chain
   - Scenario 4: Star with Bad Hub
   - Scenario 5: Flapping Test
   - Scenario 6: Storm Trigger

3. **Create Visualizations**
   - Implement serial log parser (Phase 2)
   - Generate static network graphs
   - Plot ETX evolution over time
   - Create animated visualizations

4. **Statistical Analysis**
   - Run each scenario 3-5 times
   - Calculate confidence intervals
   - Compare to theoretical predictions
   - Generate publication-ready figures

### If Tests Fail ❌

1. **Debug Systematically**
   - Check one node at a time
   - Verify adjacency graph injection
   - Test with binary connectivity (0/100%) first
   - Add more logging if needed

2. **Simplify**
   - Test with 2 nodes only (direct link)
   - Test hello packet exchange first
   - Test basic routing before ETX
   - Add instrumentation to LoRaMesher code

3. **Ask for Help**
   - Share monitor logs
   - Share simConfiguration.json
   - Share summary.json
   - Describe expected vs actual behavior

---

## Quick Reference

### Essential Commands

```bash
# Create new experiment
python main.py experiment_name

# Overwrite existing experiment
python main.py experiment_name  # Answer 'y' to overwrite prompt

# List available ports
python main.py test -p

# Check test completion
cat experiment_name/simulation_1/summary.json | grep error

# View serial logs
less experiment_name/simulation_1/Monitoring/monitor_COM9.ans

# Search for routing tables
grep -A 20 "Current routing table" experiment_name/simulation_1/Monitoring/*.ans

# Search for ETX calculations
grep "ETX for" experiment_name/simulation_1/Monitoring/*.ans
```

### Important Configuration Values

```json
{
  "SimulationTimeoutMinutes": "20",
  "Simulator": {
    "LOG_MESHER": "1",          // ← Enable detailed ETX logs
    "PACKET_COUNT": "10",
    "PACKET_DELAY": "120000"
  },
  "LoRaMesher": {
    "HELLO_PACKETS_DELAY": "60", // ← Faster convergence for testing
    "DEFAULT_TIMEOUT": "300"     // ← 5-minute timeout (5 * HELLO_DELAY)
  }
}
```

---

## Summary

**You now know:**
- ✅ How the simulation framework works
- ✅ Why these 3 scenarios were chosen
- ✅ How to create and run a test
- ✅ What to look for in the results
- ✅ How to troubleshoot common issues

**Next:** Once you've successfully run Scenario 1, we'll create the serial log parser and visualization tools to make analysis easier!

---

**Questions?** Check `ETX_Testing_Plan.md` for the full technical specification, or `ETX_IMPLEMENTATION_PLAN.md` for the phased development roadmap.
