# ETX Testing Implementation Plan

**Date:** 2025-11-04
**Branch:** feature/etx-routing-loramesh
**Status:** Ready to begin testing

---

## Current Status

### ✅ Already Implemented
- All 7 test scenarios in `topologyGenerator.py`
- Probabilistic packet loss simulation in `changeConfigurationSerial.py`
- Test framework infrastructure (main.py, simConfiguration.py, updatePlatformio.py, mqttClient.py)
- Visualization tools in `Testing/ui/`

### 🔨 To Be Implemented
1. Serial log parser for ETX metrics
2. Configuration files for initial test scenarios
3. Static network visualization tool
4. Testing documentation and expected results

---

## Phase 1: Initial Validation (Week 1)

### Goal
Confirm ETX routing works correctly with 3 fundamental test cases

### Selected Scenarios

#### 1. Scenario 1: Triangle of Trust
**Priority:** CRITICAL
**Nodes:** 3
**Objective:** Validate ETX-based routing vs hop-count

**Topology:**
```
A ←--0%--→ B ←--0%--→ C
 \\                    /
  ←-----20%----------→
```

**Expected Behavior:**
- Node A should route to C via B (2 hops, ETX ≈ 1.0)
- NOT direct A→C (1 hop, ETX ≈ 2.5 due to 20% loss)
- Routing table on A shows: "C via B"

**Success Criteria:**
- A's routing table shows route to C via B
- Total ETX to C < 1.5
- Route remains stable (no flapping)
- Serial logs show: "Found better route for C via B: old_ETX=2.5 new_ETX=1.0"

---

#### 2. Scenario 7: Loop Prevention
**Priority:** CRITICAL (Safety)
**Nodes:** 4
**Objective:** Validate duplicate packet detection

**Topology:**
```
A ←-5%-→ B
↑        ↓
5%      5%
↓        ↑
D ←-5%-→ C
```

**Expected Behavior:**
- Routing updates propagate through all paths
- Duplicate detection prevents routing loops
- Each routing update processed only once per node
- "Duplicate packet detected" logs appear

**Success Criteria:**
- Zero routing loops observed
- duplicatesDetected counter > 0 in logs
- Network converges to stable routing tables
- All nodes have routes to all other nodes

---

#### 3. Scenario 2: Asymmetric Alley
**Priority:** HIGH (Innovation validation)
**Nodes:** 4
**Objective:** Test bidirectional ETX calculation

**Topology:**
```
A --[5%/40%]-- B --[5%/40%]-- C --[5%/40%]-- D
   (forward/reverse loss rates)
```

**Expected Behavior:**
- Forward ETX (A→B) ≈ 1.05 (based on 95% ACK rate)
- Reverse ETX (B→A) ≈ 1.67 (based on 60% hello reception)
- Asymmetry ratio detected and logged
- Different route quality in each direction

**Success Criteria:**
- Forward and reverse ETX calculated independently
- Asymmetry logs show ratio ~1:3
- Routes account for both directions
- Serial logs show both ETX values correctly

---

## Testing Workflow

### Step 1: Generate Configuration Files
Run the scenario generator to create test configurations:

```bash
cd Testing
python generateScenarioConfigs.py
```

This creates:
- `scenarios/scenario_1_triangle_of_trust.json`
- `scenarios/scenario_7_loop_prevention.json`
- `scenarios/scenario_2_asymmetric_alley.json`

### Step 2: Run Tests
For each scenario:

```bash
# Copy scenario config to active config
cp scenarios/scenario_1_triangle_of_trust.json simConfiguration.json

# Run the test
python main.py

# This will:
# 1. Modify source files with adjacency graph
# 2. Build firmware for all devices
# 3. Upload to devices
# 4. Monitor serial output
# 5. Collect MQTT data
```

### Step 3: Analyze Results
Parse serial logs and generate analysis:

```bash
python analyzeETXTest.py --scenario scenario_1_triangle_of_trust
```

This produces:
- Parsed ETX metrics
- Routing table evolution
- Route change timeline
- Static network visualization
- Success/failure report

---

## Deliverables

### 1. Serial Log Parser (`Testing/analyzeETXTest.py`)
Parses ETX-specific logs from serial monitor output:
- Routing table snapshots
- ETX calculations (forward/reverse)
- Route change events
- Triggered update logs
- Storm prevention events
- Duplicate detection events

### 2. Scenario Configuration Generator (`Testing/generateScenarioConfigs.py`)
Generates complete test configurations for each scenario:
- Loads topology from `topologyGenerator.py`
- Maps to available devices (10 devices)
- Sets appropriate test parameters
- Exports to `scenarios/*.json`

### 3. Static Network Visualizer (`Testing/visualizeETXTest.py`)
Creates static visualizations:
- Network topology with loss rates
- Routing tables as directed graphs
- ETX values on edges
- Multiple time snapshots

### 4. Test Report Generator (`Testing/generateETXReport.py`)
Creates comprehensive test report:
- Success/failure for each scenario
- Key metrics extracted
- Comparison to expected behavior
- Recommendations for fixes

---

## Success Metrics

### For Scenario 1 (Triangle of Trust)
- ✅ Node A routes to C via B (not direct)
- ✅ ETX to C via B < ETX to C direct
- ✅ Route stable for entire test duration

### For Scenario 7 (Loop Prevention)
- ✅ Zero routing loops detected
- ✅ duplicatesDetected > 0
- ✅ All nodes have complete routing tables
- ✅ Network converges within 10 minutes

### For Scenario 2 (Asymmetric Alley)
- ✅ Forward ETX ≈ 1.05 for A→B link
- ✅ Reverse ETX ≈ 1.67 for A→B link
- ✅ Asymmetry ratio logged
- ✅ Routes reflect bidirectional calculation

---

## Phase 2: Advanced Testing (Week 2+)

After Phase 1 succeeds, expand to:
- Scenario 3: Degrading Chain
- Scenario 4: Star with Bad Hub
- Scenario 5: Flapping Test
- Scenario 6: Storm Trigger

And add:
- Animated network visualization
- MQTT routing table snapshots
- Time-series ETX plots
- Statistical analysis and confidence intervals

---

## Next Steps

1. ✅ Review and approve this plan
2. 🔨 Implement serial log parser
3. 🔨 Implement scenario config generator
4. 🔨 Implement static visualizer
5. 🔨 Create testing documentation
6. ▶️ Run Scenario 1 test
7. ▶️ Debug and fix issues
8. ▶️ Run Scenarios 2 and 7
9. ▶️ Document results

---

## Questions Before Implementation

1. **Device Addresses:** What are the actual addresses of your 10 devices? (Currently using placeholders 1000-10000)
2. **COM Ports:** What are the COM ports / serial devices for your hardware?
3. **PlatformIO Environments:** Which environments are you using? (ttgo-t-beam, ttgo-lora32-v1, etc.)
4. **Test Duration:** How long should each test run? (Suggested: 15-20 minutes for initial tests)
5. **Baseline:** Do you want to run a "baseline" test with binary connectivity (0%/100%) first to confirm existing functionality?

Please answer these questions and I'll proceed with implementation.
