# ETX Routing Testing & Visualization Plan

**Date:** 2025-11-04
**Purpose:** Comprehensive testing strategy for bidirectional ETX routing implementation
**Target:** 10-device LoRaChat network with LoRaMesher library

---

## Table of Contents
1. [Testing Requirements & Objectives](#testing-requirements--objectives)
2. [Current System Architecture](#current-system-architecture)
3. [Software Packet Loss Simulation](#software-packet-loss-simulation)
4. [Predefined Test Topologies](#predefined-test-topologies)
5. [Enhanced Data Collection](#enhanced-data-collection)
6. [Visualization Architecture](#visualization-architecture)
7. [Implementation Roadmap](#implementation-roadmap)
8. [Scientific Validation Metrics](#scientific-validation-metrics)

---

## 1. Testing Requirements & Objectives

### 1.1 Primary Objectives

**Verify ETX Routing Correctness:**
- [ ] Routes selected based on link quality (ETX) not hop count
- [ ] Bidirectional ETX calculated correctly (forward + reverse)
- [ ] Route selection uses 10% hysteresis to prevent flapping
- [ ] Routes adapt when link quality changes

**Verify Loop Prevention:**
- [ ] Duplicate packet detection works (sourceAddress, packetId)
- [ ] No routing loops occur even with triggered updates
- [ ] Circular buffer cache functions correctly (50 entries, 5-min timeout)

**Verify Storm Prevention:**
- [ ] Rate limiting prevents update flooding (5s minimum interval)
- [ ] Per-route cooldown works (10s per destination)
- [ ] Exponential backoff activates on storm detection
- [ ] Backoff counter increases/decreases correctly

**Verify Asymmetric Link Handling:**
- [ ] Forward and reverse ETX tracked independently
- [ ] Asymmetric links (A→B ≠ B→A) handled correctly
- [ ] Routes adapt to asymmetry ratios

### 1.2 Success Criteria

**Functional Requirements:**
- Network converges to stable routing tables within 10 minutes (5 hello cycles)
- Triggered updates reduce convergence time by >50% vs periodic-only
- No routing loops detected in any test scenario
- Packet delivery rate improves by >20% in poor link conditions vs hop-count routing

**Performance Requirements:**
- Memory overhead <30% vs baseline (measured via `getFreeHeap()`)
- Routing table updates complete within 2 hello intervals
- Storm prevention keeps triggered updates <10 per minute per device
- Duplicate detection accuracy >99%

**Scientific Validation:**
- Statistical significance (p < 0.05) for delivery rate improvements
- Confidence intervals calculated for all metrics (95% CI)
- Results reproducible across 5+ runs per topology

---

## 2. Current System Architecture

### 2.1 Testing Framework Overview

**Workflow:** `main.py` → `simConfiguration.py` → `changeConfigurationSerial.py` → `updatePlatformio.py` → `mqttClient.py` → `ui/`

**Key Components:**
1. **Configuration Creation** (simConfiguration.py)
   - Interactive user prompts for test parameters
   - Device mapping (COM ports → PlatformIO environments)
   - Adjacency graph definition
   - Saves to `simConfiguration.json`

2. **Configuration Injection** (changeConfigurationSerial.py)
   - Modifies `src/config.h` with simulator settings
   - Modifies `BuildOptions.h` with LoRaMesher parameters
   - **Injects adjacency graph into `LoraMesher.cpp::canReceivePacket()`**

3. **Device Orchestration** (updatePlatformio.py)
   - Build phase: Parallel compilation
   - Upload phase: Flash all devices
   - Monitor phase: Capture serial output

4. **Data Collection** (mqttClient.py)
   - MQTT broker: localhost:1883, topic `to-server/#`
   - Stores messages in `data.json` with timestamps
   - Serial logs saved to `Monitoring/monitor_{port}.ans`

5. **Analysis & Visualization** (ui/)
   - Per-device metrics (RTT, timeouts, message loss, heap)
   - Cross-experiment comparison with confidence intervals
   - Publication export (LaTeX, CSV, Markdown)

### 2.2 Adjacency Graph Injection Mechanism

**Current Implementation:**
- File: `changeConfigurationSerial.py::changeAdjacencyGraph()`
- Target: `LoRaMesher.cpp::canReceivePacket(uint16_t source)`
- Method: Replaces function body with auto-generated C++ code

**Generated C++ Code Structure:**
```cpp
bool LoraMesher::canReceivePacket(uint16_t source) {
#ifdef TESTING
    uint16_t localAddress = getLocalAddress();
    uint16_t adjacencyGraphSize = 10;  // Number of nodes
    uint16_t headers[adjacencyGraphSize] = {20056, 20068, 22212, ...};  // Node IDs

    // Find local node index
    uint16_t localAddressIndex = 0;
    for (int i = 0; i < adjacencyGraphSize; i++) {
        if (headers[i] == localAddress) {
            localAddressIndex = i;
            break;
        }
    }

    // Find source node index
    uint16_t sourceIndex = 0;
    for (int i = 0; i < adjacencyGraphSize; i++) {
        if (headers[i] == source) {
            sourceIndex = i;
            break;
        }
    }

    // Adjacency matrix (0 = no link, 1 = direct link)
    uint16_t const matrix[adjacencyGraphSize][adjacencyGraphSize] = {
        { 1, 1, 0, 0, ... },  // Node 0 neighbors
        { 1, 1, 1, 0, ... },  // Node 1 neighbors
        ...
    };

    return matrix[localAddressIndex][sourceIndex] != 0;
#else
    return true;  // Production: accept all packets
#endif
}
```

**Current Limitation:** Binary connectivity (0/1) only. **Need to extend for packet loss probabilities.**

### 2.3 ETX Logging Already in Place

**What We Added (feature/etx-bidirectional-routing):**

1. **Routing Table with ETX** (`RoutingTableService.cpp::printRoutingTable()`)
   ```
   Current routing table:
   0 - 4E38 via 4E44 ETX=2.3 (R:1.1+F:1.2) asym:0.92 Role:0
       └─ Direct: hellos=95/100 (95.0%) SNR=8
   ```

2. **ETX Calculation Logs** (`RoutingTableService.cpp::calculateReverseETX/ForwardETX()`)
   ```
   Reverse ETX for 4E38: received=95 expected=100 rate=95.00% ETX=1.05 metric=11
   Forward ETX for 4E38: acks=18 sent=20 rate=90.00% ETX=1.11 metric=11
   ```

3. **Route Change Logs** (`RoutingTableService.cpp::processRoute()`)
   ```
   Found better route for 4E38 via 4E44: old_ETX=1.5+1.5=3.0 new_ETX=1.1+1.2=2.3
   ```

4. **Triggered Update Logs** (`LoraMesher.cpp::sendTriggeredHelloPacket()`)
   ```
   Sending triggered hello packet (reason=1)
   Triggered hello complete: packets=1 routes=9
   ```

5. **Storm Prevention Logs** (`RouteUpdateService.cpp`)
   ```
   Triggered update suppressed: backoff=10000ms time_since_last=3000ms
   Update storm detected: backoff_counter=2
   ```

6. **Duplicate Detection Logs** (`RouteUpdateService.cpp`)
   ```
   Duplicate packet detected: src=4E38 id=123 age=15000ms
   ```

**Data Flow:**
- All logs go to serial output → captured by `updatePlatformio.py`
- Saved to `Monitoring/monitor_{port}.ans`
- Can be parsed post-test for analysis

### 2.4 MQTT Data Collection

**Current Usage:**
- Devices send simulation results to MQTT
- Topic format: `to-server/{device_address}`
- Message format: JSON with test data

**Example Message:**
```json
{
  "topic": "to-server/20056",
  "payload": {
    "data": {
      "messageType": "SimMessage",
      "timestamp": 1234567890,
      "metrics": { /* device metrics */ }
    }
  },
  "date": "2025-11-04 14:30:45"
}
```

**Opportunity:** We can add routing table snapshots to MQTT messages for real-time tracking.

---

## 3. Software Packet Loss Simulation

### 3.1 Design Overview

**Approach:** Extend `canReceivePacket()` to probabilistically drop packets based on per-link loss rates.

**Configuration Format:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,      20056, 20068, 22212, ...],  // Row 0: Node IDs
    [20056,  0,     5,     20,    ...],  // Row 1: Node 20056 link qualities (% loss)
    [20068,  5,     0,     10,    ...],  // Row 2: Node 20068 link qualities
    ...
  ]
}
```

**Interpretation:**
- `matrix[i][j]` = packet loss percentage from node i to node j
- `0` = perfect link (0% loss)
- `5` = 5% packet loss
- `20` = 20% packet loss
- `100` = no link (100% loss)

**Asymmetric Links Supported:**
- `matrix[A][B] != matrix[B][A]` → A→B and B→A have different loss rates
- Example: A→B = 5%, B→A = 20% (realistic for different TX powers)

### 3.2 Implementation Details

**Modified C++ Code Generation:**

```cpp
bool LoraMesher::canReceivePacket(uint16_t source) {
#ifdef TESTING
    uint16_t localAddress = getLocalAddress();
    uint16_t adjacencyGraphSize = 10;
    uint16_t headers[adjacencyGraphSize] = {20056, 20068, ...};

    // Find indices (same as before)
    uint16_t localAddressIndex = 0;
    for (int i = 0; i < adjacencyGraphSize; i++) {
        if (headers[i] == localAddress) {
            localAddressIndex = i;
            break;
        }
    }

    uint16_t sourceIndex = 0;
    for (int i = 0; i < adjacencyGraphSize; i++) {
        if (headers[i] == source) {
            sourceIndex = i;
            break;
        }
    }

    // Loss rate matrix (values 0-100 representing % loss)
    uint16_t const lossRateMatrix[adjacencyGraphSize][adjacencyGraphSize] = {
        { 0,  5,  20, 100, ... },  // Node 0 loss rates to others
        { 5,  0,  10, 100, ... },  // Node 1 loss rates
        ...
    };

    uint16_t lossRate = lossRateMatrix[sourceIndex][localAddressIndex];

    // 100% loss = no link
    if (lossRate >= 100) {
        return false;
    }

    // 0% loss = perfect link
    if (lossRate == 0) {
        return true;
    }

    // Probabilistic packet loss
    uint32_t randomValue = random(0, 100);  // 0-99
    bool shouldDrop = (randomValue < lossRate);

    if (shouldDrop) {
        ESP_LOGV(LM_TAG, "Packet from %X dropped (simulated loss: %u%%)",
                 source, lossRate);
    }

    return !shouldDrop;
#else
    return true;
#endif
}
```

**Python Code Update (`changeConfigurationSerial.py::get_cpp_function()`):**

```python
def get_cpp_function(self, matrix):
    adjacencyGraphInCpp = "\tuint16_t localAddress = getLocalAddress();\n"

    # Extract headers
    headers = matrix[0][1:]
    adjacencyGraphInCpp += f"\tuint16_t adjacencyGraphSize = {len(headers)};\n"

    # Add headers array
    adjacencyGraphInCpp += "\tuint16_t headers[adjacencyGraphSize] = {"
    adjacencyGraphInCpp += ", ".join(str(header) for header in headers)
    adjacencyGraphInCpp += "};\n"

    # Find local address index
    adjacencyGraphInCpp += "\tuint16_t localAddressIndex = 0;\n"
    adjacencyGraphInCpp += "\tfor (int i = 0; i < adjacencyGraphSize; i++) {\n"
    adjacencyGraphInCpp += "\t\tif (headers[i] == localAddress) {\n"
    adjacencyGraphInCpp += "\t\t\tlocalAddressIndex = i;\n"
    adjacencyGraphInCpp += "\t\t\tbreak;\n"
    adjacencyGraphInCpp += "\t\t}\n"
    adjacencyGraphInCpp += "\t}\n"

    # Find source index
    adjacencyGraphInCpp += "\tuint16_t sourceIndex = 0;\n"
    adjacencyGraphInCpp += "\tfor (int i = 0; i < adjacencyGraphSize; i++) {\n"
    adjacencyGraphInCpp += "\t\tif (headers[i] == source) {\n"
    adjacencyGraphInCpp += "\t\t\tsourceIndex = i;\n"
    adjacencyGraphInCpp += "\t\t\tbreak;\n"
    adjacencyGraphInCpp += "\t\t}\n"
    adjacencyGraphInCpp += "\t}\n"

    # Remove headers from matrix
    matrix = np.delete(matrix, 0, 0)
    matrix = np.delete(matrix, 0, 1)

    # Create C++ loss rate matrix
    cpp_matrix = ",\n\t\t".join(
        "{ " + ", ".join(str(cell) for cell in row) + " }" for row in matrix
    )

    adjacencyGraphInCpp += f"""
\tuint16_t const lossRateMatrix[adjacencyGraphSize][adjacencyGraphSize] = {{
\t\t{cpp_matrix}
\t}};
\t
\tuint16_t lossRate = lossRateMatrix[sourceIndex][localAddressIndex];
\t
\t// 100% loss = no link
\tif (lossRate >= 100) {{
\t\treturn false;
\t}}
\t
\t// 0% loss = perfect link
\tif (lossRate == 0) {{
\t\treturn true;
\t}}
\t
\t// Probabilistic packet loss
\tuint32_t randomValue = random(0, 100);
\tbool shouldDrop = (randomValue < lossRate);
\t
\tif (shouldDrop) {{
\t\tESP_LOGV(LM_TAG, "Packet from %X dropped (simulated loss: %u%%)", source, lossRate);
\t}}
\t
\treturn !shouldDrop;
"""

    return adjacencyGraphInCpp
```

### 3.3 Link Quality Classification

**Recommended Categories:**

| Category | Loss Rate | Expected ETX | Use Case |
|----------|-----------|--------------|----------|
| Excellent | 0-2% | 1.0-1.02 | Ideal conditions, close proximity |
| Good | 3-10% | 1.03-1.11 | Normal operation, moderate distance |
| Fair | 11-25% | 1.12-1.33 | Marginal links, obstacles present |
| Poor | 26-50% | 1.35-2.0 | Unreliable, high interference |
| Very Poor | 51-75% | 2.04-4.0 | Barely usable, frequent retransmissions |
| Unusable | 76-99% | 4.17-100 | Connection drops frequently |
| No Link | 100% | ∞ | No connectivity |

**Asymmetry Scenarios:**
- **Moderate Asymmetry:** A→B = 5%, B→A = 15% (ratio 1:3)
- **High Asymmetry:** A→B = 10%, B→A = 40% (ratio 1:4)
- **Extreme Asymmetry:** A→B = 5%, B→A = 70% (ratio 1:14)

### 3.4 Testing & Validation

**Verify Packet Loss Simulation:**
1. Create topology with known loss rates
2. Send 1000 packets over each link
3. Measure actual loss rate vs configured
4. Expected variance: ±5% (binomial distribution)

**Example Validation Test:**
```python
# Configuration: A→B = 20% loss
# Send 1000 packets from A to B
# Expected: ~200 dropped (180-220 acceptable with 95% CI)
```

---

## 4. Predefined Test Topologies

### 4.1 Design Philosophy

**Approach:** Curated test scenarios that systematically test specific ETX behaviors.

**Rationale:**
- **Predefined scenarios** allow targeted testing of specific features
- **Reproducible** results across test runs
- **Scientifically valid** - can explain WHY results occur
- **Debuggable** - easier to identify issues than random topologies

**Complement with:**
- **Real-world inspired patterns** for ecological validity
- **Stress tests** with random topologies (future work)

### 4.2 Core Test Scenarios

#### **Scenario 1: "The Triangle of Trust"**
**Purpose:** Verify ETX route selection over hop count

```
Topology:
    A ←---50%---→ C
    ↑              ↑
   5%             5%
    ↓              ↓
    B ←---5%----→ D

Path options from A to C:
- Direct: A→C (1 hop, ETX = 2.0 due to 50% loss)
- Via B: A→B→C (2 hops, ETX = 1.05 + 1.05 = 1.10)

Expected: A chooses A→B→C despite more hops
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,   "A",  "B",  "C"],
    ["A",  0,    5,   50],
    ["B",  5,    0,    5],
    ["C",  50,   5,    0]
  ]
}
```

**Success Criteria:**
- A's routing table shows route to C via B (not direct)
- Total ETX to C ≈ 1.10
- Route stable (no flapping)

---

#### **Scenario 2: "Asymmetric Alley"**
**Purpose:** Test bidirectional ETX with asymmetric links

```
Topology:
    A ←---10%/40%---→ B ←---5%/5%---→ C
       (A→B: 10%)        (symmetric)
       (B→A: 40%)

Forward/Reverse ETX:
- A→B: forward=1.11 (90% ACK rate), reverse=1.67 (60% hello rate)
- B→A: forward=1.67 (60% ACK rate), reverse=1.11 (90% hello rate)
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,   "A",  "B",  "C"],
    ["A",  0,   10,  100],
    ["B",  40,   0,    5],
    ["C", 100,   5,    0]
  ]
}
```

**Success Criteria:**
- A correctly measures forward ETX ≈ 1.11, reverse ETX ≈ 1.67
- B correctly measures forward ETX ≈ 1.67, reverse ETX ≈ 1.11
- Asymmetry ratio logged and detected
- Routes account for both directions

---

#### **Scenario 3: "The Degrading Chain"**
**Purpose:** Test route adaptation as link quality degrades over time

```
Topology (time-variant):
Phase 1 (0-5 min):   A ←5%→ B ←5%→ C ←5%→ D
Phase 2 (5-10 min):  A ←25%→ B ←5%→ C ←5%→ D
Phase 3 (10-15 min): A ←50%→ B ←5%→ C ←5%→ D

Expected behavior:
- Phase 1: Direct A→B works well
- Phase 2: ETX increases, triggered update sent
- Phase 3: If alternate path exists, switch occurs
```

**Implementation:** Requires manual configuration change or scripted topology updates.

**Success Criteria:**
- ETX values tracked over time show degradation
- Triggered updates sent when link quality drops significantly
- Alternative routes discovered if available

---

#### **Scenario 4: "Star with Bad Hub"**
**Purpose:** Test route selection when central node has poor links

```
Topology:
        B ←5%→ E
        ↑       ↑
       5%      5%
        ↓       ↓
    A ←40%→ C ←40%→ D
            ↑
           40%
            ↓
            F

Hub C has 40% loss to all neighbors
Edge nodes (A,B,D,E,F) have 5% loss between each other

Expected: Traffic routes around C, not through it
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,   "A",  "B",  "C",  "D",  "E",  "F"],
    ["A",  0,    5,   40,  100,  100,  100],
    ["B",  5,    0,   40,  100,    5,  100],
    ["C",  40,  40,    0,   40,   40,   40],
    ["D", 100, 100,   40,    0,    5,    5],
    ["E", 100,   5,   40,    5,    0,  100],
    ["F", 100, 100,   40,    5,  100,    0]
  ]
}
```

**Success Criteria:**
- Traffic from A to D avoids C (goes via B or E)
- C is not selected as next hop unless no alternative
- Route convergence within 10 minutes

---

#### **Scenario 5: "The Flapping Test"**
**Purpose:** Verify hysteresis prevents route flapping

```
Topology:
    A ←12%→ B ←12%→ C
    ↓                ↑
   15%              15%
    ↓                ↑
    D ←---12%------→ E

Two paths from A to E:
- Path 1: A→B→C→E (ETX ≈ 3.4)
- Path 2: A→D→E (ETX ≈ 3.4)

Paths have similar ETX - hysteresis should prevent constant switching
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,   "A",  "B",  "C",  "D",  "E"],
    ["A",  0,   12, 100,   15, 100],
    ["B",  12,   0,  12,  100, 100],
    ["C", 100,  12,   0,  100,  15],
    ["D",  15, 100, 100,    0,  12],
    ["E", 100, 100,  15,   12,   0]
  ]
}
```

**Success Criteria:**
- Route changes <3 times in 30 minutes (stable)
- Hysteresis logs show "route change prevented" messages
- No continuous flapping between similar ETX routes

---

#### **Scenario 6: "Storm Trigger"**
**Purpose:** Test storm prevention mechanisms

```
Topology:
Full mesh of 10 nodes, all with 5% loss

At t=5min: Simultaneously introduce 50% loss on multiple links
This should trigger many routing updates

Expected: Storm prevention limits updates, backoff increases
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    // 10x10 matrix, all values = 5
    // (then change to 50 for selected links at t=5min)
  ]
}
```

**Success Criteria:**
- Triggered updates initially sent
- Storm detection activates (backoff counter increases)
- Updates suppressed (rate limiting)
- Network still converges despite rate limiting
- No more than 10 updates/minute/device

---

#### **Scenario 7: "Loop Prevention"**
**Purpose:** Verify duplicate detection prevents routing loops

```
Topology:
    A ←5%→ B ←5%→ C
    ↓              ↑
   5%            5%
    ↓              ↑
    D ←---5%----→ E

Circular topology where packets could loop

Expected: Duplicate detection catches repeated packets
```

**Configuration:**
```json
{
  "LoRaMesherAdjacencyGraph": [
    [0,   "A",  "B",  "C",  "D",  "E"],
    ["A",  0,    5, 100,    5, 100],
    ["B",  5,    0,   5,  100, 100],
    ["C", 100,   5,   0,  100,   5],
    ["D",  5,  100, 100,    0,   5],
    ["E", 100, 100,   5,    5,   0]
  ]
}
```

**Success Criteria:**
- Zero routing loops detected
- Duplicate packet logs show caught duplicates
- Each packet ID processed only once per node
- Cache functions correctly (FIFO, timeout)

---

### 4.3 Real-World Inspired Topologies

#### **Topology A: "Indoor Office"**
**Based on:** Real-world LoRa indoor deployment research

```
Characteristics:
- 10 nodes distributed across 3 floors
- Walls cause 20-40% loss between floors
- Same-floor links have 5-15% loss
- One node per floor acts as "gateway" to other floors
```

**Research Reference:** Urban LoRa deployment patterns (IEEE papers)

---

#### **Topology B: "Outdoor Linear (Road)"**
**Based on:** Vehicular network patterns

```
Characteristics:
- Nodes arranged linearly (simulating road)
- Distance-based loss: closer nodes = better links
- 1-hop: 5% loss
- 2-hop: 15% loss
- 3-hop: 35% loss
- 4+hop: 60% loss (degrading)
```

**Use Case:** Highway monitoring, agricultural sensor networks

---

#### **Topology C: "Small-World Network"**
**Based on:** Watts-Strogatz model

```
Characteristics:
- Most nodes connected to nearby neighbors (5% loss)
- Few random "long-distance" links (30-50% loss)
- Tests ETX preference for short high-quality vs long poor-quality
```

**Research Value:** Common in social networks, applicable to mesh networks

---

### 4.4 Topology Generator Tool

**New Python Module:** `Testing/topologyGenerator.py`

```python
class TopologyGenerator:
    def __init__(self, num_nodes):
        self.num_nodes = num_nodes
        self.node_ids = []

    def generate_scenario(self, scenario_name):
        """Generate predefined scenario by name"""
        if scenario_name == "triangle_of_trust":
            return self._generate_triangle_of_trust()
        elif scenario_name == "asymmetric_alley":
            return self._generate_asymmetric_alley()
        # ... other scenarios

    def generate_random_geometric(self, node_positions, transmission_range):
        """Generate topology based on 2D node positions"""
        # Calculates distance-based loss rates
        pass

    def generate_small_world(self, k, p):
        """Generate Watts-Strogatz small-world network"""
        pass

    def export_to_adjacency_matrix(self):
        """Export to simConfiguration.json format"""
        pass
```

**Integration:** Called from `simConfiguration.py` during configuration creation.

---

## 5. Enhanced Data Collection

### 5.1 Routing Table Snapshot System

**Objective:** Capture routing tables from all devices periodically to visualize evolution.

#### **Implementation Approach:**

**Option A: MQTT Broadcast (Recommended)**
- Periodically send routing table snapshot via MQTT
- Triggered by: (1) Timer (every 60s), (2) Significant route change
- Minimal code changes to existing system

**Option B: Serial Log Parsing**
- Parse `printRoutingTable()` output from serial logs
- No MQTT overhead, but requires post-processing
- Good for initial testing

**Recommendation:** Implement both, use MQTT for real-time, serial for validation.

#### **Data Format:**

**MQTT Topic:** `to-server/routing-table/{device_address}`

**Message Format:**
```json
{
  "messageType": "RoutingTableSnapshot",
  "timestamp": 1699123456789,
  "deviceAddress": "0x4E38",
  "routes": [
    {
      "destination": "0x4E44",
      "via": "0x4E44",
      "reverseETX": 11,
      "forwardETX": 12,
      "totalETX": 23,
      "helloPacketsExpected": 100,
      "helloPacketsReceived": 95,
      "dataPacketsSent": 20,
      "ackPacketsReceived": 18,
      "timeout": 1699123500000,
      "receivedSNR": 8,
      "isDirect": true
    },
    {
      "destination": "0x5A12",
      "via": "0x4E44",
      "reverseETX": 25,
      "forwardETX": 28,
      "totalETX": 53,
      "helloPacketsExpected": 0,
      "helloPacketsReceived": 0,
      "dataPacketsSent": 0,
      "ackPacketsReceived": 0,
      "timeout": 1699123500000,
      "receivedSNR": 0,
      "isDirect": false
    }
  ],
  "statistics": {
    "routeCount": 9,
    "directNeighborCount": 3,
    "multiHopRouteCount": 6,
    "averageETX": 2.5,
    "maxETX": 5.2,
    "minETX": 1.1
  }
}
```

#### **Code Changes:**

**File:** `LoRaMesher library → Add RoutingTableService::getRoutingTableSnapshot()`

```cpp
// RoutingTableService.h
static String getRoutingTableSnapshotJSON();

// RoutingTableService.cpp
String RoutingTableService::getRoutingTableSnapshotJSON() {
    StaticJsonDocument<4096> doc;

    doc["messageType"] = "RoutingTableSnapshot";
    doc["timestamp"] = millis();
    doc["deviceAddress"] = String(WiFiService::getLocalAddress(), HEX);

    JsonArray routes = doc.createNestedArray("routes");

    routingTableList->setInUse();
    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            JsonObject route = routes.createNestedObject();
            route["destination"] = String(node->networkNode.address, HEX);
            route["via"] = String(node->via, HEX);
            route["reverseETX"] = node->networkNode.reverseETX;
            route["forwardETX"] = node->networkNode.forwardETX;
            route["totalETX"] = node->networkNode.reverseETX + node->networkNode.forwardETX;
            route["helloPacketsExpected"] = node->helloPacketsExpected;
            route["helloPacketsReceived"] = node->helloPacketsReceived;
            route["dataPacketsSent"] = node->dataPacketsSent;
            route["ackPacketsReceived"] = node->ackPacketsReceived;
            route["timeout"] = node->timeout;
            route["receivedSNR"] = node->receivedSNR;
            route["isDirect"] = (node->via == node->networkNode.address);

        } while (routingTableList->next());
    }
    routingTableList->releaseInUse();

    // Add statistics
    JsonObject stats = doc.createNestedObject("statistics");
    stats["routeCount"] = routingTableSize();
    // ... calculate other stats

    String output;
    serializeJson(doc, output);
    return output;
}
```

**File:** `src/monitor/monService.cpp` (existing monitoring service)

```cpp
// Add periodic routing table broadcast
void MonitoringService::sendRoutingTableSnapshot() {
    String json = RoutingTableService::getRoutingTableSnapshotJSON();

    MQTTQueueMessageV2* mqttMessage = new MQTTQueueMessageV2();
    mqttMessage->body = json;
    mqttMessage->topic = String("to-server/routing-table/") +
                         String(WiFiService::getLocalAddress(), HEX);

    MqttService::getInstance().sendMqttMessage(mqttMessage);
    delete mqttMessage;
}

// Call this every 60 seconds in monitoring loop
```

#### **Python Collection:**

**File:** `Testing/mqttClient.py` (extend existing)

```python
# Add handler for routing table messages
def on_message(client, userdata, msg):
    payload_str = msg.payload.decode()

    if "routing-table" in msg.topic:
        # Store routing table snapshots separately
        save_routing_table_snapshot(msg.topic, payload_str)
    else:
        # Existing data collection
        save_to_data_json(msg.topic, payload_str)

def save_routing_table_snapshot(topic, payload):
    # Save to routing_tables.json with timestamp indexing
    data = {
        "topic": topic,
        "payload": json.loads(payload),
        "date": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }

    file_path = os.path.join(experiment_dir, "routing_tables.json")
    append_to_json_array(file_path, data)
```

### 5.2 ETX Metrics Export

**Additional Metrics to Track:**

1. **Per-Link ETX History**
   - Track ETX values over time for each direct neighbor
   - Detect trends (improving/degrading)

2. **Route Change Events**
   - Log every route change with: timestamp, destination, old route, new route, reason
   - Calculate route stability metrics

3. **Storm Prevention Stats**
   - Triggered updates sent vs suppressed
   - Backoff counter evolution
   - Per-device update frequency

4. **Duplicate Detection Stats**
   - Total duplicates caught
   - Duplicate rate per source
   - Cache hit rate

**Data Storage:**
```
experiment_name/
├── simulation_1/
│   ├── data.json                 # Existing: simulation results
│   ├── routing_tables.json       # NEW: Routing table snapshots
│   ├── route_changes.json        # NEW: Route change log
│   ├── storm_prevention.json     # NEW: Storm prevention stats
│   └── Monitoring/
│       └── monitor_COM9.ans      # Existing: Serial logs (everything)
```

### 5.3 Serial Log Parsing Tool

**New Module:** `Testing/monitoringAnalysis/parseETXLogs.py`

```python
def parse_etx_logs(monitor_file):
    """
    Parse ETX-specific logs from serial monitor output
    Returns structured data for analysis
    """
    with open(monitor_file, 'r') as f:
        logs = f.readlines()

    parsed_data = {
        "routing_tables": [],
        "etx_calculations": [],
        "route_changes": [],
        "triggered_updates": [],
        "storm_prevention": [],
        "duplicates_detected": []
    }

    for line in logs:
        # Parse routing table prints
        if "Current routing table:" in line:
            table = parse_routing_table_block(logs, start_index)
            parsed_data["routing_tables"].append(table)

        # Parse ETX calculations
        elif "Reverse ETX for" in line:
            etx_data = extract_etx_calculation(line)
            parsed_data["etx_calculations"].append(etx_data)

        # Parse route changes
        elif "Found better route" in line:
            change = extract_route_change(line)
            parsed_data["route_changes"].append(change)

        # ... other log types

    return parsed_data
```

---

## 6. Visualization Architecture

### 6.1 Overview

**Goals:**
1. **Animated network graph** showing routing evolution
2. **Time-series plots** for ETX metrics
3. **Scientific validation** visualizations (statistical comparisons)
4. **Publication-ready** figures

**Technology Stack:**
- **NetworkX:** Graph manipulation and layout algorithms
- **Matplotlib:** Plotting and animation
- **Pandas:** Data processing and time-series analysis
- **Seaborn:** Statistical visualizations

### 6.2 Animated Network Graph

**New Module:** `Testing/ui/animatedNetworkGraph.py`

#### **Features:**

1. **Node Representation:**
   - Color: Role (gateway=red, regular=blue)
   - Size: Proportional to routing table size
   - Label: Device address (hex)

2. **Edge Representation:**
   - Line thickness: Inverse of ETX (thicker = better link)
   - Color gradient: ETX value (green=good, yellow=fair, red=poor)
   - Label: ETX value displayed on hover
   - Style: Solid=direct neighbor, dashed=multi-hop route

3. **Animation:**
   - Time slider: Scrub through routing table snapshots
   - Play/pause controls
   - Speed control (0.5x to 4x)
   - Export to video (MP4) or GIF

4. **Interactive:**
   - Click node: Show full routing table
   - Click edge: Show link quality history
   - Highlight path: Select source/destination to highlight route

#### **Implementation:**

```python
import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.widgets import Slider, Button
import json

class AnimatedNetworkGraph:
    def __init__(self, routing_tables_file):
        self.routing_tables = self.load_routing_tables(routing_tables_file)
        self.timestamps = sorted(self.routing_tables.keys())
        self.current_frame = 0

        self.fig, self.ax = plt.subplots(figsize=(14, 10))
        self.G = nx.Graph()
        self.pos = None  # Node positions (fixed layout)

    def load_routing_tables(self, file_path):
        """Load routing table snapshots grouped by timestamp"""
        with open(file_path, 'r') as f:
            data = json.load(f)

        # Group by timestamp
        tables_by_time = {}
        for entry in data:
            timestamp = entry['payload']['timestamp']
            device = entry['payload']['deviceAddress']

            if timestamp not in tables_by_time:
                tables_by_time[timestamp] = {}

            tables_by_time[timestamp][device] = entry['payload']['routes']

        return tables_by_time

    def build_graph_at_timestamp(self, timestamp):
        """Build NetworkX graph from routing tables at specific time"""
        G = nx.Graph()
        routes_at_time = self.routing_tables[timestamp]

        # Add nodes
        for device, routes in routes_at_time.items():
            G.add_node(device, routing_table_size=len(routes))

        # Add edges (only direct neighbors)
        for device, routes in routes_at_time.items():
            for route in routes:
                if route['isDirect']:
                    dest = route['destination']
                    total_etx = route['totalETX'] / 10.0  # Unscale

                    G.add_edge(device, dest,
                              etx=total_etx,
                              reverse_etx=route['reverseETX'] / 10.0,
                              forward_etx=route['forwardETX'] / 10.0)

        return G

    def get_edge_color(self, etx):
        """Map ETX to color (green=good, red=bad)"""
        if etx < 1.2:
            return 'green'
        elif etx < 2.0:
            return 'yellow'
        elif etx < 4.0:
            return 'orange'
        else:
            return 'red'

    def get_edge_width(self, etx):
        """Map ETX to line width (thicker=better)"""
        return max(0.5, 5.0 / etx)

    def draw_frame(self, frame_idx):
        """Draw network graph at specific frame"""
        self.ax.clear()

        timestamp = self.timestamps[frame_idx]
        G = self.build_graph_at_timestamp(timestamp)

        # Use fixed layout (spring layout computed once)
        if self.pos is None:
            self.pos = nx.spring_layout(G, k=0.5, iterations=50)

        # Draw nodes
        node_sizes = [G.nodes[node].get('routing_table_size', 5) * 100
                     for node in G.nodes()]
        nx.draw_networkx_nodes(G, self.pos,
                               node_size=node_sizes,
                               node_color='lightblue',
                               ax=self.ax)

        # Draw edges with ETX-based styling
        for (u, v, data) in G.edges(data=True):
            etx = data['etx']
            edge_color = self.get_edge_color(etx)
            edge_width = self.get_edge_width(etx)

            nx.draw_networkx_edges(G, self.pos, [(u, v)],
                                   edge_color=edge_color,
                                   width=edge_width,
                                   ax=self.ax)

            # Draw ETX label on edge
            x = (self.pos[u][0] + self.pos[v][0]) / 2
            y = (self.pos[u][1] + self.pos[v][1]) / 2
            self.ax.text(x, y, f"{etx:.1f}", fontsize=8,
                        bbox=dict(facecolor='white', alpha=0.7))

        # Draw node labels
        nx.draw_networkx_labels(G, self.pos, font_size=10, ax=self.ax)

        self.ax.set_title(f"Network Routing at t={timestamp}ms", fontsize=14)
        self.ax.axis('off')

    def animate(self):
        """Create animated visualization"""
        def update(frame):
            self.draw_frame(frame)

        anim = animation.FuncAnimation(self.fig, update,
                                      frames=len(self.timestamps),
                                      interval=1000,  # 1 second per frame
                                      repeat=True)

        plt.show()
        return anim

    def export_video(self, output_file, fps=2):
        """Export animation to MP4 video"""
        Writer = animation.writers['ffmpeg']
        writer = Writer(fps=fps, metadata=dict(artist='ETX Testing'),
                       bitrate=1800)

        def update(frame):
            self.draw_frame(frame)

        anim = animation.FuncAnimation(self.fig, update,
                                      frames=len(self.timestamps),
                                      repeat=False)

        anim.save(output_file, writer=writer)
        print(f"Video saved to {output_file}")

# Usage
if __name__ == "__main__":
    graph = AnimatedNetworkGraph("experiment/simulation_1/routing_tables.json")
    graph.animate()
    # graph.export_video("network_evolution.mp4", fps=2)
```

### 6.3 ETX Time-Series Plots

**New Module:** `Testing/ui/drawETXTimeSeries.py`

#### **Plot Types:**

1. **Per-Link ETX Evolution**
   - X-axis: Time (seconds)
   - Y-axis: ETX value
   - Multiple lines: One per direct neighbor link
   - Shows how link quality changes over time

2. **Route Stability Plot**
   - X-axis: Time
   - Y-axis: Number of active routes
   - Annotations: Route change events
   - Shows network convergence

3. **Storm Prevention Activity**
   - X-axis: Time
   - Y-axis: Triggered updates per minute
   - Line colors: Sent (green), Suppressed (red)
   - Shows rate limiting effectiveness

#### **Implementation:**

```python
import matplotlib.pyplot as plt
import pandas as pd
import json
from datetime import datetime

class ETXTimeSeriesPlotter:
    def __init__(self, routing_tables_file, device_filter=None):
        self.data = self.load_and_process_data(routing_tables_file, device_filter)

    def load_and_process_data(self, file_path, device_filter):
        """Load routing tables and convert to time-series DataFrame"""
        with open(file_path, 'r') as f:
            raw_data = json.load(f)

        records = []
        for entry in raw_data:
            device = entry['payload']['deviceAddress']
            if device_filter and device not in device_filter:
                continue

            timestamp = entry['payload']['timestamp']
            for route in entry['payload']['routes']:
                if route['isDirect']:  # Only direct neighbors
                    records.append({
                        'timestamp': timestamp,
                        'device': device,
                        'neighbor': route['destination'],
                        'total_etx': route['totalETX'] / 10.0,
                        'reverse_etx': route['reverseETX'] / 10.0,
                        'forward_etx': route['forwardETX'] / 10.0,
                        'hello_rate': (route['helloPacketsReceived'] /
                                      route['helloPacketsExpected'] * 100
                                      if route['helloPacketsExpected'] > 0 else 0),
                        'ack_rate': (route['ackPacketsReceived'] /
                                    route['dataPacketsSent'] * 100
                                    if route['dataPacketsSent'] > 0 else 0)
                    })

        df = pd.DataFrame(records)
        df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
        return df

    def plot_link_etx_evolution(self, device, neighbor=None):
        """Plot ETX evolution for specific device's links"""
        device_data = self.data[self.data['device'] == device]

        if neighbor:
            device_data = device_data[device_data['neighbor'] == neighbor]

        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

        # Plot 1: Total ETX over time
        for neighbor_id in device_data['neighbor'].unique():
            neighbor_data = device_data[device_data['neighbor'] == neighbor_id]
            ax1.plot(neighbor_data['timestamp'], neighbor_data['total_etx'],
                    label=f"Neighbor {neighbor_id}", marker='o', markersize=3)

        ax1.set_ylabel('Total ETX', fontsize=12)
        ax1.set_title(f'Link Quality Evolution for Device {device}', fontsize=14)
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # Plot 2: Forward vs Reverse ETX
        for neighbor_id in device_data['neighbor'].unique():
            neighbor_data = device_data[device_data['neighbor'] == neighbor_id]
            ax2.plot(neighbor_data['timestamp'], neighbor_data['reverse_etx'],
                    label=f"{neighbor_id} (Reverse)", linestyle='--')
            ax2.plot(neighbor_data['timestamp'], neighbor_data['forward_etx'],
                    label=f"{neighbor_id} (Forward)", linestyle='-')

        ax2.set_xlabel('Time', fontsize=12)
        ax2.set_ylabel('ETX (Forward/Reverse)', fontsize=12)
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        plt.show()

    def plot_asymmetry_detection(self):
        """Plot asymmetry ratios over time"""
        # Calculate asymmetry ratio: reverse ETX / forward ETX
        plot_data = self.data.copy()
        plot_data['asymmetry_ratio'] = (plot_data['reverse_etx'] /
                                        plot_data['forward_etx'])

        fig, ax = plt.subplots(figsize=(12, 6))

        for (device, neighbor), group in plot_data.groupby(['device', 'neighbor']):
            ax.plot(group['timestamp'], group['asymmetry_ratio'],
                   label=f"{device}→{neighbor}", alpha=0.7)

        # Mark significant asymmetry (ratio > 1.5 or < 0.67)
        ax.axhline(y=1.5, color='r', linestyle='--',
                  label='High Asymmetry Threshold')
        ax.axhline(y=0.67, color='r', linestyle='--')
        ax.axhline(y=1.0, color='g', linestyle=':', label='Perfect Symmetry')

        ax.set_xlabel('Time', fontsize=12)
        ax.set_ylabel('Asymmetry Ratio (Reverse/Forward)', fontsize=12)
        ax.set_title('Link Asymmetry Detection', fontsize=14)
        ax.legend()
        ax.grid(True, alpha=0.3)

        plt.tight_layout()
        plt.show()

# Usage
if __name__ == "__main__":
    plotter = ETXTimeSeriesPlotter("experiment/simulation_1/routing_tables.json",
                                   device_filter=["0x4E38", "0x4E44"])
    plotter.plot_link_etx_evolution("0x4E38")
    plotter.plot_asymmetry_detection()
```

### 6.4 Scientific Validation Visualizations

**Extend existing:** `Testing/ui/drawExperimentComparisonCI.py`

#### **New Plots:**

1. **ETX vs Hop Count Comparison**
   - Compare packet delivery rate: ETX routing vs hop-count routing
   - Show confidence intervals (95% CI)
   - Statistical significance testing (t-test)

2. **Convergence Time Analysis**
   - X-axis: Time (0-15 minutes)
   - Y-axis: % of routes stable
   - Compare: Periodic-only vs Triggered updates
   - Show when network reaches equilibrium

3. **Storm Prevention Effectiveness**
   - Bar chart: Updates sent vs suppressed
   - Compare with/without storm prevention
   - Show backoff counter evolution

4. **Route Flapping Frequency**
   - Histogram: Route changes per destination
   - Compare: With/without hysteresis
   - Show stability improvement

#### **Implementation:**

```python
def plot_etx_vs_hopcount_comparison(etx_experiment_dir, hopcount_experiment_dir):
    """
    Compare packet delivery rate between ETX and hop-count routing
    """
    # Load data from both experiments
    etx_data = load_experiment_data(etx_experiment_dir)
    hopcount_data = load_experiment_data(hopcount_experiment_dir)

    # Calculate delivery rates
    etx_delivery_rates = calculate_delivery_rates(etx_data)
    hopcount_delivery_rates = calculate_delivery_rates(hopcount_data)

    # Calculate confidence intervals
    etx_mean, etx_ci = calculate_ci(etx_delivery_rates)
    hopcount_mean, hopcount_ci = calculate_ci(hopcount_delivery_rates)

    # Statistical significance test
    t_stat, p_value = ttest_ind(etx_delivery_rates, hopcount_delivery_rates)

    # Plotting
    fig, ax = plt.subplots(figsize=(10, 6))

    x = ['Hop Count', 'ETX Routing']
    means = [hopcount_mean, etx_mean]
    cis = [hopcount_ci, etx_ci]

    ax.bar(x, means, yerr=cis, capsize=10, alpha=0.7,
           color=['#FF6B6B', '#4ECDC4'])

    # Add significance annotation
    if p_value < 0.05:
        significance = f"p={p_value:.4f} (significant)"
        ax.text(0.5, max(means) * 1.1, significance,
               ha='center', fontsize=12,
               bbox=dict(boxstyle='round', facecolor='yellow', alpha=0.5))

    ax.set_ylabel('Packet Delivery Rate (%)', fontsize=12)
    ax.set_title('ETX vs Hop-Count Routing Performance', fontsize=14)
    ax.set_ylim(0, 105)
    ax.grid(axis='y', alpha=0.3)

    plt.tight_layout()
    plt.savefig('etx_vs_hopcount.pdf', dpi=300, bbox_inches='tight')
    plt.show()
```

### 6.5 Export for Publication

**Extend existing:** `Testing/ui/exportResults.py`

#### **New Exports:**

1. **ETX Summary Table** (LaTeX/CSV)
   - Per-topology statistics
   - Delivery rate, convergence time, route stability
   - Confidence intervals included

2. **Link Quality Matrix** (Heatmap)
   - Matrix showing all pairwise ETX values
   - Color-coded for easy identification
   - Export as high-resolution PNG/PDF

3. **Animation Frames** (for presentations)
   - Export key frames from animated graph
   - Before/after topology change
   - Convergence snapshots

---

## 7. Implementation Roadmap

### 7.1 Development Phases

#### **Phase 1: Core Packet Loss Simulation (Week 1)**
**Priority:** High - Foundational for all testing

**Tasks:**
1. [ ] Modify `changeConfigurationSerial.py::get_cpp_function()`
   - Add loss rate matrix generation
   - Add probabilistic packet drop logic
   - Test with simple 3-node topology

2. [ ] Create `topologyGenerator.py`
   - Implement 7 predefined scenarios
   - Export to adjacency matrix format
   - Integration with `simConfiguration.py`

3. [ ] Validation test
   - Run 1000-packet test with known loss rates
   - Verify actual vs configured loss (±5%)

**Deliverables:**
- Working packet loss simulation
- 7 predefined topologies ready to use
- Validation report

---

#### **Phase 2: Enhanced Data Collection (Week 2)**
**Priority:** High - Needed for visualization

**Tasks:**
1. [ ] Implement routing table snapshot system
   - Add `RoutingTableService::getRoutingTableSnapshotJSON()`
   - Integrate with MQTT broadcasting
   - Test data collection

2. [ ] Extend `mqttClient.py`
   - Add `routing_tables.json` handler
   - Separate storage for routing snapshots

3. [ ] Create `parseETXLogs.py`
   - Parse ETX calculations from serial logs
   - Extract route changes
   - Extract storm prevention events

**Deliverables:**
- Real-time routing table collection via MQTT
- Serial log parser for post-test analysis
- Data storage structure validated

---

#### **Phase 3: Visualization Tools (Week 3)**
**Priority:** Medium - For analysis and presentation

**Tasks:**
1. [ ] Implement `animatedNetworkGraph.py`
   - Basic network graph rendering
   - ETX-based edge styling
   - Animation with time slider
   - Export to video

2. [ ] Implement `drawETXTimeSeries.py`
   - Link ETX evolution plots
   - Asymmetry detection plots
   - Storm prevention activity

3. [ ] Extend comparison plots
   - ETX vs hop-count comparison
   - Convergence time analysis
   - Route stability histograms

**Deliverables:**
- Animated network visualization
- Time-series ETX plots
- Publication-ready figures

---

#### **Phase 4: Testing & Validation (Week 4)**
**Priority:** High - Scientific validation

**Tasks:**
1. [ ] Run predefined scenarios (7 topologies)
   - 5 runs per topology for statistical validity
   - Collect all data (MQTT + serial logs)

2. [ ] Analyze results
   - Calculate confidence intervals
   - Statistical significance tests
   - Generate all visualizations

3. [ ] Compare with baseline (hop-count routing)
   - Rerun with ETX disabled (if possible)
   - Compare delivery rates, convergence times
   - Document improvements

**Deliverables:**
- Test results for all 7 scenarios
- Statistical analysis report
- Comparison with baseline

---

### 7.2 File Modification Summary

**Modified Files:**
```
Testing/
├── changeConfigurationSerial.py          [MODIFY] - Add loss rate matrix
├── topologyGenerator.py                  [NEW]    - Predefined topologies
├── mqttClient.py                         [MODIFY] - Routing table handler
├── simConfiguration.py                   [MODIFY] - Integrate topology generator
└── monitoringAnalysis/
    └── parseETXLogs.py                   [NEW]    - Serial log parser

Testing/ui/
├── animatedNetworkGraph.py               [NEW]    - Animated visualization
├── drawETXTimeSeries.py                  [NEW]    - Time-series plots
├── drawExperimentComparisonCI.py         [MODIFY] - Add ETX comparisons
└── exportResults.py                      [MODIFY] - Export ETX metrics

LoRaMesher Library/
└── src/
    ├── services/
    │   └── RoutingTableService.cpp       [MODIFY] - Add snapshot JSON method
    └── monitor/
        └── monService.cpp                [MODIFY] - Add periodic snapshot broadcast
```

**Estimated Lines of Code:**
- Packet loss simulation: ~100 lines
- Topology generator: ~300 lines
- Data collection enhancements: ~200 lines
- Visualization tools: ~500 lines
- **Total: ~1100 lines of new/modified code**

---

## 8. Scientific Validation Metrics

### 8.1 Quantitative Metrics

**Primary Metrics:**
1. **Packet Delivery Rate (PDR)**
   - Formula: `(packets_delivered / packets_sent) × 100%`
   - Target: >20% improvement over hop-count in poor link scenarios
   - Statistical test: Two-sample t-test (α=0.05)

2. **Convergence Time**
   - Definition: Time until all routes stable (no changes for 5 minutes)
   - Target: <10 minutes (5 hello cycles)
   - With triggered updates: <50% of periodic-only time

3. **Route Stability**
   - Formula: `1 - (route_changes / total_routes)`
   - Target: >90% stability after convergence
   - Measured over 30-minute window

4. **Storm Prevention Effectiveness**
   - Metric: `updates_suppressed / (updates_sent + updates_suppressed)`
   - Target: >80% suppression during storm events
   - Verify network still converges despite suppression

### 8.2 Qualitative Assessments

**Correctness Verification:**
- [ ] Routes always select lower ETX over higher hop count
- [ ] Asymmetric links correctly tracked (forward ≠ reverse)
- [ ] Hysteresis prevents unnecessary route changes
- [ ] No routing loops detected (duplicate detection 100% effective)

**Robustness Testing:**
- [ ] Network recovers from node failures
- [ ] Handles sudden topology changes (link quality drops)
- [ ] Maintains performance with 10 devices (scalability)
- [ ] Memory overhead acceptable (<30% increase)

### 8.3 Reporting Standards

**Follow CI_GUIDE.md standards:**
- Report mean ± 95% CI for all metrics
- Include sample size (n=5 minimum)
- State statistical test used and p-values
- Show raw data in supplementary tables
- Use consistent terminology

**Publication Checklist:**
- [ ] All figures have error bars (95% CI)
- [ ] Statistical significance clearly marked
- [ ] Methods section describes test setup
- [ ] Reproducibility: Configuration files included
- [ ] Code availability: GitHub repository linked

---

## 9. Next Steps & Open Questions

### 9.1 Immediate Actions

**For User Review:**
1. Review predefined topologies - are they sufficient?
2. Approve data collection approach (MQTT vs serial parsing)
3. Confirm visualization priorities

**For Implementation:**
1. Start with Phase 1 (packet loss simulation)
2. Validate with simple 3-node test
3. Proceed to data collection once validated

### 9.2 Open Questions

1. **Topology Dynamics:**
   - Q: Should we implement time-variant topologies (link quality changes during test)?
   - Suggestion: Manual configuration change at specific time points for now

2. **Real-time vs Post-test Visualization:**
   - Q: Is real-time animated graph during test useful, or only post-test playback?
   - Suggestion: Post-test only initially, add real-time if needed

3. **Baseline Comparison:**
   - Q: Can we disable ETX and revert to hop-count for baseline comparison?
   - Suggestion: May need to test on older LoRaMesher version or add compile-time flag

4. **Statistical Power:**
   - Q: Is n=5 sufficient for all scenarios?
   - Suggestion: Start with n=5, increase to n=10 if variance is high

5. **Additional Visualizations:**
   - Q: What other plots would be scientifically valuable?
   - Suggestions welcome based on research goals

---

## 10. Conclusion

This ETX Testing Plan provides a comprehensive framework for:
- **Validating** ETX routing implementation correctness
- **Measuring** performance improvements over hop-count routing
- **Visualizing** routing behavior and network evolution
- **Reporting** results with scientific rigor

**Key Innovations:**
1. Software packet loss simulation (no hardware changes needed)
2. Predefined topologies for systematic testing
3. Real-time routing table tracking via MQTT
4. Animated network visualization
5. Publication-ready statistical analysis

**Expected Outcomes:**
- Scientific validation of ETX routing benefits
- Identification of edge cases and limitations
- High-quality visualizations for presentations/papers
- Reproducible testing methodology

**Ready for Implementation:** Upon approval, begin with Phase 1 (packet loss simulation).

---

**Document Status:** Draft for Review
**Next Update:** After user feedback and Phase 1 completion

