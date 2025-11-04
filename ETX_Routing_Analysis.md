# ETX-Based Routing Analysis for LoRaMesher

**Date**: 2025-10-17
**Author**: Analysis for LoRaChat project
**Target**: LoRaMesher library improvements

---

## Executive Summary

The current LoRaMesher routing algorithm uses **hop count** as the sole routing metric, which causes suboptimal path selection in large networks (30+ devices). This document proposes implementing **Expected Transmission Count (ETX)** based on packet reception rate to select routes based on link quality rather than distance.

---

## Current Implementation Analysis

### Routing Algorithm Overview

**Type**: Distance-vector routing (similar to RIP)
**Location**: `.pio/libdeps/ttgo-t-beam/LoRaMesher/src/services/RoutingTableService.cpp`

**Current Behavior**:
```cpp
// Line 87: For each advertised node, increment hop count
node->metric++;

// Lines 115-120: Route selection prefers lower hop count
if (node->metric < rNode->networkNode.metric) {
    rNode->networkNode.metric = node->metric;
    rNode->via = via;
    resetTimeoutRoutingNode(rNode);
}
```

### Routing Table Structure

**Location**: `RouteNode.h` (lines 10-62)

```cpp
class RouteNode {
    NetworkNode networkNode;      // Address (2B) + metric (1B) + role (1B)
    uint32_t timeout;             // Route expiration time
    uint16_t via;                 // Next hop address
    int8_t receivedSNR;           // SNR from received packets (1-hop only)
    int8_t sentSNR;               // SNR from sent packets (1-hop only)
    unsigned long SRTT;           // Smoothed round-trip time
    unsigned long RTTVAR;         // RTT variation
};
```

**Size per entry**: ~24 bytes
**Maximum entries**: 256 nodes (RTMAXSIZE)
**Total memory**: ~6 KB maximum

### Hello Packet Mechanism

**Location**: `LoraMesher.cpp` (lines 600-649)

**Characteristics**:
- **Interval**: 120 seconds (HELLO_PACKETS_DELAY)
- **Timeout**: 600 seconds (5 × hello interval)
- **Packet Structure**:
  - Header: 8 bytes (dst, src, type, id, packetSize)
  - Node Role: 1 byte
  - Network Nodes: 4 bytes each (address + metric + role)
- **Max nodes per packet**: 22 nodes (91 bytes / 4 bytes per node)
- **Priority**: DEFAULT_PRIORITY + 4 (high priority)

**Hello Packet Content**:
```cpp
RoutePacket {
    uint16_t dst;              // Broadcast
    uint16_t src;              // Sender address
    uint8_t type;              // HELLO_P (0x04)
    uint8_t id;                // Packet ID
    uint8_t packetSize;        // Total size
    uint8_t nodeRole;          // Sender's role
    NetworkNode networkNodes[]; // Known routes
}
```

---

## Problem Statement

### Issue 1: Ignoring Link Quality

**Current behavior**: Route selection based solely on hop count

**Example scenario**:
```
Network topology:
  A ←─────50%──────→ D (1 hop, 50% packet reception)
  A ←─95%→ B ←─95%→ C ←─95%→ D (3 hops, 95% on each link)

Current choice: Direct path A→D (metric = 1)
Better choice:  Path A→B→C→D (higher reliability)
```

**Impact**:
- More retransmissions on unreliable direct links
- Higher timeout rates
- Increased collision probability (retransmissions consume more airtime)
- Poor end-to-end delivery rate

### Issue 2: Collision Between Hello and Data Packets

**Problem**: Hello packets (9-100 bytes) collide with larger data packets
- Hello packets: Typically 9-40 bytes (small routing tables)
- Data packets: Up to 100 bytes
- **Larger packets have higher collision probability** due to longer time-on-air

**Contributing factors**:
1. Hello packets sent at high priority (DEFAULT_PRIORITY + 4)
2. All nodes send hello packets around the same time (synchronized timing)
3. No jitter or random delay in hello transmission
4. Hidden terminal problem in multi-hop networks

**Current mitigation**: Random backoff delay (`waitBeforeSend()`) only partially helps

---

## Proposed Solution: ETX-Based Routing

### What is ETX?

**ETX (Expected Transmission Count)**: The expected number of transmissions (including retransmissions) needed to successfully deliver a packet over a link.

**Formula**:
```
ETX = 1 / packet_reception_rate

Where packet_reception_rate = packets_received / packets_expected
```

**Examples**:
| Reception Rate | ETX | Meaning |
|----------------|-----|---------|
| 100% | 1.0 | Perfect link (1 attempt) |
| 90% | 1.11 | Very good link |
| 75% | 1.33 | Good link |
| 50% | 2.0 | Poor link (2 attempts on avg) |
| 25% | 4.0 | Very poor link (4 attempts) |
| 10% | 10.0 | Extremely unreliable |

**Multi-hop ETX** is additive:
```
Route A→B→C→D:
  ETX_total = ETX(A→B) + ETX(B→C) + ETX(C→D)

Example:
  ETX(A→B) = 1.2 (83% success)
  ETX(B→C) = 1.1 (91% success)
  ETX(C→D) = 1.5 (67% success)
  Total ETX = 1.2 + 1.1 + 1.5 = 3.8

Compare to direct:
  ETX(A→D) = 4.0 (25% success)

→ Multi-hop route is better despite more hops!
```

### Implementation Strategy

#### Phase 1: Add Reception Tracking

**File**: `RouteNode.h`

**Add to RouteNode class**:
```cpp
// Reception rate tracking (only for 1-hop neighbors)
uint16_t helloPacketsExpected = 0;  // Expected hello packets since route created
uint16_t helloPacketsReceived = 0;  // Actually received hello packets
```

**Memory impact**: 4 bytes per route entry
**For 10 direct neighbors**: 40 bytes
**For 20 direct neighbors**: 80 bytes

#### Phase 2: Calculate Link ETX

**File**: `RoutingTableService.cpp`

**New helper function**:
```cpp
uint8_t RoutingTableService::calculateLinkETX(uint16_t address) {
    RouteNode* node = findNode(address);

    if (node == nullptr || node->via != address) {
        // Not a direct neighbor, return default
        return 10; // ETX = 1.0
    }

    // Require minimum sample size for reliability
    if (node->helloPacketsExpected < 3) {
        return 10; // ETX = 1.0 (optimistic bootstrap)
    }

    // Calculate reception rate
    float receptionRate = (float)node->helloPacketsReceived /
                          (float)node->helloPacketsExpected;

    // Prevent division by zero
    if (receptionRate < 0.04) {
        receptionRate = 0.04; // Cap at ETX = 25.0
    }

    // Calculate ETX scaled by 10 (to fit in uint8_t)
    // ETX range: 1.0 - 25.5 → metric range: 10 - 255
    float etx = 1.0 / receptionRate;
    uint8_t metric = (uint8_t)(etx * 10.0);

    // Clamp to valid range
    if (metric < 10) metric = 10;
    if (metric > 255) metric = 255;

    return metric;
}
```

#### Phase 3: Update Route Processing

**File**: `RoutingTableService.cpp`

**Modify `processRoute()` function** (line 70):
```cpp
void RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR) {
    // ... existing validation code ...

    size_t numNodes = p->getNetworkNodesSize();

    // Process direct sender as 1-hop neighbor
    RouteNode* senderNode = findNode(p->src);
    if (senderNode != nullptr) {
        // Increment received hello counter
        senderNode->helloPacketsReceived++;

        // Update SNR
        senderNode->receivedSNR = receivedSNR;
    } else {
        // New neighbor - create with initial ETX = 1.0
        NetworkNode* receivedNode = new NetworkNode(p->src, 10, p->nodeRole);
        processRoute(p->src, receivedNode);
        delete receivedNode;

        // Initialize counters for bootstrap
        senderNode = findNode(p->src);
        if (senderNode != nullptr) {
            senderNode->helloPacketsExpected = 1;
            senderNode->helloPacketsReceived = 1;
        }
    }

    // Calculate sender's link ETX
    uint8_t senderLinkETX = calculateLinkETX(p->src);

    // Process advertised nodes
    for (size_t i = 0; i < numNodes; i++) {
        NetworkNode* node = &p->networkNodes[i];

        // ADD sender's link ETX instead of incrementing by 1
        node->metric += senderLinkETX;

        processRoute(p->src, node);
    }

    printRoutingTable();
}
```

**Modify `processRoute(uint16_t via, NetworkNode* node)` function** (line 104):
```cpp
void RoutingTableService::processRoute(uint16_t via, NetworkNode* node) {
    if (node->address != WiFiService::getLocalAddress()) {
        RouteNode* rNode = findNode(node->address);

        if (rNode == nullptr) {
            addNodeToRoutingTable(node, via);
            return;
        }

        // Update if better ETX metric found
        if (node->metric < rNode->networkNode.metric) {
            ESP_LOGI(LM_TAG, "Found better route for %X via %X: old_metric=%d new_metric=%d (ETX: %.1f → %.1f)",
                     node->address, via,
                     rNode->networkNode.metric, node->metric,
                     rNode->networkNode.metric / 10.0, node->metric / 10.0);

            rNode->networkNode.metric = node->metric;
            rNode->via = via;
            resetTimeoutRoutingNode(rNode);
        }
        else if (node->metric == rNode->networkNode.metric) {
            resetTimeoutRoutingNode(rNode);
        }

        // Update role if needed
        if (getNextHop(node->address) == via && node->role != rNode->networkNode.role) {
            rNode->networkNode.role = node->role;
        }
    }
}
```

#### Phase 4: Periodic Counter Management

**File**: `LoraMesher.cpp`

**Modify `sendHelloPacket()` function** (line 600):
```cpp
void LoraMesher::sendHelloPacket() {
    ESP_LOGV(LM_TAG, "Send Hello Packet routine started");
    vTaskSuspend(NULL);

    size_t maxNodesPerPacket = (PacketFactory::getMaxPacketSize() - sizeof(RoutePacket)) / sizeof(NetworkNode);

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    for (;;) {
        // UPDATE: Increment expected hello counter for all direct neighbors
        RoutingTableService::updateExpectedHelloPackets();

        // ... existing hello packet creation and sending code ...

        vTaskDelay(HELLO_PACKETS_DELAY * 1000 / portTICK_PERIOD_MS);
    }
}
```

**File**: `RoutingTableService.cpp`

**New function**:
```cpp
void RoutingTableService::updateExpectedHelloPackets() {
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            // Only update for direct neighbors (1-hop)
            if (node->via == node->networkNode.address) {
                node->helloPacketsExpected++;

                // Apply exponential decay every 10 hellos to prevent overflow
                // and give more weight to recent measurements
                if (node->helloPacketsExpected >= 100) {
                    node->helloPacketsExpected = (node->helloPacketsExpected * 8) / 10;
                    node->helloPacketsReceived = (node->helloPacketsReceived * 8) / 10;

                    ESP_LOGV(LM_TAG, "Applied decay to reception counters for %X: exp=%d rcv=%d",
                             node->networkNode.address,
                             node->helloPacketsExpected,
                             node->helloPacketsReceived);
                }
            }
        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}
```

### Metric Scaling for 8-bit Field

Since `NetworkNode.metric` is `uint8_t` (0-255), we scale ETX by 10×:

| Reception Rate | Actual ETX | Metric (10×) | Interpretation |
|----------------|------------|--------------|----------------|
| 100% | 1.0 | 10 | Perfect link |
| 95% | 1.05 | 11 | Excellent |
| 90% | 1.11 | 11 | Very good |
| 80% | 1.25 | 13 | Good |
| 75% | 1.33 | 13 | Good |
| 67% | 1.50 | 15 | Acceptable |
| 50% | 2.0 | 20 | Poor |
| 33% | 3.0 | 30 | Very poor |
| 25% | 4.0 | 40 | Extremely poor |
| 20% | 5.0 | 50 | Nearly unusable |
| 10% | 10.0 | 100 | Unusable |
| 4% | 25.0 | 250 | Maximum (capped) |

**Dynamic range**: 1.0 - 25.5 (metric 10-255)
**Precision**: 0.1 ETX units
**Maximum path ETX**: 25.5 (with capping)

---

## Asymmetric Link Problem

### What Are Asymmetric Links?

**Asymmetric link**: A wireless link where the quality differs in each direction.

**Example**:
```
Link A→B: 90% reception (ETX = 1.1)
Link B→A: 50% reception (ETX = 2.0)
```

### Why Do Asymmetric Links Occur?

1. **Different transmit power**: Node A transmits at 20 dBm, Node B at 14 dBm
2. **Antenna differences**: Node A has better antenna than Node B
3. **Interference patterns**: Node B experiences more interference when receiving
4. **Physical obstacles**: Terrain/buildings block one direction more than another
5. **Hardware differences**: Different receiver sensitivity between devices

### Impact on ETX Routing

#### The Problem

**Simple ETX approach**:
- Node B measures incoming link quality (A→B) from hello packets
- Node B advertises this quality in its own hello packets
- Node A uses this to calculate routes TO node B
- **But**: When A sends to B, it uses A→B quality
- **Problem**: B measured and advertised B→A quality, not A→B!

**Scenario**:
```
Reality:
  A→B: 50% success (ETX = 2.0)  ← What A needs to know
  B→A: 90% success (ETX = 1.1)  ← What B measures

What happens:
  1. B receives 90% of A's hello packets
  2. B advertises: "Route to A has ETX = 1.1"
  3. A thinks: "Great! I can reach B with ETX = 1.1"
  4. A sends data to B... but only 50% arrives!
  5. Result: Underestimated path cost, more retransmissions
```

#### Quantifying the Impact

**Severity depends on asymmetry ratio**:

| A→B ETX | B→A ETX | Asymmetry Ratio | Impact |
|---------|---------|-----------------|--------|
| 1.1 | 1.2 | 1.09× | Minimal - negligible effect |
| 1.5 | 1.0 | 1.5× | Moderate - 50% underestimate |
| 2.0 | 1.0 | 2.0× | High - routing suboptimal |
| 4.0 | 1.0 | 4.0× | Severe - frequent failures |

**Real-world observations**:
- LoRa networks: Typically 1.1-1.5× asymmetry (manageable)
- High power imbalance: Can reach 2-3× asymmetry
- Extreme cases: 5-10× asymmetry (rare, usually hardware issues)

### Solutions for Asymmetric Links

#### Option 1: Bidirectional ETX (Recommended)

**Concept**: Measure and advertise BOTH directions

**Implementation**:
1. Each node tracks:
   - **Forward link ETX**: Success rate of packets SENT (from ACKs)
   - **Reverse link ETX**: Success rate of packets RECEIVED (from hello packets)

2. Hello packet modification:
   ```cpp
   // Add to NetworkNode structure (extend to 6 bytes):
   class NetworkNode {
       uint16_t address;
       uint8_t metric;        // Reverse link ETX (current)
       uint8_t forwardETX;    // NEW: Forward link ETX
       uint8_t role;
   }
   ```

3. Calculation:
   ```cpp
   // Node B measures:
   reverse_ETX = 1 / (hello_rcv_from_A / hello_expected_from_A)  // B←A quality
   forward_ETX = 1 / (ack_rcv_from_A / data_sent_to_A)          // B→A quality

   // Node B advertises both in hello packet
   // Node A uses forward_ETX when routing to B
   ```

**Pros**:
- ✅ Accurate representation of both link directions
- ✅ Optimal path selection
- ✅ Works with all asymmetry scenarios

**Cons**:
- ❌ Requires protocol change (NetworkNode: 4 bytes → 6 bytes)
- ❌ Reduces max nodes per hello packet (22 → 15 nodes)
- ❌ Requires tracking ACK success rate (added complexity)
- ❌ Not backward compatible

#### Option 2: Assume Symmetry (Simplest)

**Concept**: Use reverse link ETX as approximation for forward link ETX

**Implementation**: Use the simple ETX approach described earlier (no changes)

**Pros**:
- ✅ No protocol changes needed
- ✅ Simple implementation
- ✅ Works well when asymmetry < 1.5×
- ✅ Backward compatible

**Cons**:
- ❌ Underestimates cost on asymmetric links
- ❌ Can select suboptimal routes in asymmetric scenarios

**When to use**:
- Networks with similar hardware (same transmit power)
- LoRa networks (typically symmetric within 1.2×)
- Initial implementation (can upgrade later)

#### Option 3: Maximum ETX (Conservative)

**Concept**: Use maximum of forward and reverse ETX

**Implementation**:
```cpp
// Node B tracks both directions
reverse_ETX = calculateFromHelloPackets();
forward_ETX = calculateFromAckSuccess();

// Advertise the WORSE of the two
advertised_metric = max(forward_ETX, reverse_ETX) * 10;
```

**Pros**:
- ✅ Conservative routing (avoids bad links)
- ✅ No protocol change (fits in existing metric field)
- ✅ Handles asymmetry safely

**Cons**:
- ❌ Requires tracking ACK success rate
- ❌ May avoid links that are good in one direction
- ❌ Pessimistic for symmetric links

#### Option 4: Geometric Mean ETX (Balanced)

**Concept**: Use geometric mean to balance both directions

**Implementation**:
```cpp
// Node B calculates:
forward_ETX = calculateFromAckSuccess();
reverse_ETX = calculateFromHelloPackets();

// Geometric mean gives balanced estimate
bidirectional_ETX = sqrt(forward_ETX * reverse_ETX);
advertised_metric = bidirectional_ETX * 10;
```

**Pros**:
- ✅ Balanced approach
- ✅ No protocol change
- ✅ Better than pure reverse ETX

**Cons**:
- ❌ Requires tracking ACK success rate
- ❌ Still underestimates in highly asymmetric cases
- ❌ More complex than simple approach

### Recommended Approach

**Phase 1 (Initial Implementation)**: **Option 2 - Assume Symmetry**
- Simplest to implement
- No protocol changes
- Good enough for most LoRa networks
- Can evaluate asymmetry impact in real deployments

**Phase 2 (If asymmetry is problematic)**: **Option 3 - Maximum ETX**
- Add ACK tracking to measure forward link quality
- Use maximum of forward/reverse ETX
- Still no protocol change required

**Phase 3 (If protocol change acceptable)**: **Option 1 - Bidirectional ETX**
- Extend NetworkNode to 6 bytes (add forwardETX field)
- Full bidirectional awareness
- Optimal routing

### Measuring Asymmetry in Your Network

Add logging to measure asymmetry:

```cpp
void RoutingTableService::printRoutingTableWithAsymmetry() {
    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* node = routingTableList->getCurrent();

            if (node->via == node->networkNode.address) { // Direct neighbor
                float reverse_rate = (float)node->helloPacketsReceived /
                                     (float)node->helloPacketsExpected;
                float reverse_ETX = 1.0 / reverse_rate;

                // Calculate forward ETX from ACK success (if available)
                float forward_rate = calculateAckSuccessRate(node->networkNode.address);
                float forward_ETX = 1.0 / forward_rate;

                float asymmetry_ratio = forward_ETX / reverse_ETX;

                ESP_LOGI(LM_TAG, "Node %X: Fwd_ETX=%.2f Rev_ETX=%.2f Asymmetry=%.2fx %s",
                         node->networkNode.address,
                         forward_ETX,
                         reverse_ETX,
                         asymmetry_ratio,
                         (asymmetry_ratio > 1.5) ? "⚠️ HIGH" : "✓");
            }
        } while (routingTableList->next());
    }

    routingTableList->releaseInUse();
}
```

---

## Summary of Files to Modify

### 1. RouteNode.h
**Changes**:
- Add `uint16_t helloPacketsExpected`
- Add `uint16_t helloPacketsReceived`

**Lines**: 10-62
**Memory impact**: +4 bytes per route entry
**Estimated total**: +40-100 bytes for typical network

### 2. RoutingTableService.h
**Changes**:
- Add `uint8_t calculateLinkETX(uint16_t address)` method
- Add `void updateExpectedHelloPackets()` method

**Lines**: Header file (add to public methods)

### 3. RoutingTableService.cpp
**Changes**:
- Implement `calculateLinkETX()` function
- Implement `updateExpectedHelloPackets()` function
- Modify `processRoute(RoutePacket* p, int8_t receivedSNR)` to:
  - Increment `helloPacketsReceived` counter
  - Initialize counters for new neighbors
  - Use `senderLinkETX` instead of `metric++`
- Modify `processRoute(uint16_t via, NetworkNode* node)` to:
  - Add ETX values instead of incrementing
  - Log ETX values in route updates

**Lines**: 70-132, plus new functions
**Estimated additions**: ~100-150 lines

### 4. LoraMesher.cpp
**Changes**:
- Call `RoutingTableService::updateExpectedHelloPackets()` before sending hello packet

**Lines**: 600-649 (sendHelloPacket function)
**Estimated changes**: 1-2 lines

---

## Benefits of ETX Routing

✅ **Link quality awareness**: Routes through reliable paths instead of shortest-hop unreliable paths
✅ **Reduced retransmissions**: Better paths reduce timeouts and retries
✅ **Lower collision probability**: Fewer retransmissions = less airtime = fewer collisions
✅ **Adaptive to network conditions**: Reception rate automatically adapts to interference, obstacles, etc.
✅ **No protocol changes** (Phase 1): Uses existing 8-bit metric field
✅ **Minimal memory overhead**: ~40-100 bytes for typical networks
✅ **Simple implementation**: ~100-150 lines of code
✅ **Based on real performance**: Uses actual packet success rate, not theoretical models
✅ **Backward compatible**: Works with old firmware (they see it as hop count)

---

## Additional Recommendations

### 1. Hello Packet Collision Mitigation

**Problem**: All nodes send hello packets around the same time

**Solution**: Add random jitter to hello packet timing
```cpp
// In sendHelloPacket() function:
uint32_t jitter = random(0, 10000);  // 0-10 second jitter
vTaskDelay((HELLO_PACKETS_DELAY * 1000 + jitter) / portTICK_PERIOD_MS);
```

### 2. Hello Packet Splitting Optimization

**Current**: If routing table > 22 nodes, split into multiple packets sent immediately

**Problem**: Burst of hello packets causes collisions

**Solution**: Space out hello packet transmissions
```cpp
for (size_t i = 0; i < numPackets; ++i) {
    // ... create and send packet ...

    // Add delay between hello packets
    if (i < numPackets - 1) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);  // 2 second spacing
    }
}
```

### 3. Route Flapping Prevention

**Problem**: Rapid switching between routes with similar ETX

**Solution**: Add hysteresis to route selection
```cpp
// Only switch routes if new route is significantly better
float HYSTERESIS = 1.1;  // 10% improvement required
if (node->metric < rNode->networkNode.metric / HYSTERESIS) {
    // Switch to new route
}
```

### 4. Logging and Debugging

Add comprehensive logging for ETX values:
```cpp
ESP_LOGI(LM_TAG, "Route to %X via %X: metric=%d (ETX=%.1f) reception=%d/%d (%.0f%%)",
         node->address, via, metric, metric/10.0,
         received, expected, (received*100.0)/expected);
```

---

## Testing Plan

### 1. Unit Testing
- Test ETX calculation with various reception rates
- Test counter overflow handling (decay mechanism)
- Test bootstrap behavior (< 3 samples)

### 2. Integration Testing
- Monitor routing table changes with ETX vs hop count
- Compare route selection before/after ETX implementation
- Verify no memory leaks from new fields

### 3. Field Testing
- Deploy on subset of nodes in network
- Monitor asymmetry ratios (log forward vs reverse link quality)
- Compare packet delivery rate with hop count routing
- Measure end-to-end latency improvements

### 4. Performance Metrics to Track
- End-to-end packet delivery rate (should increase)
- Average number of retransmissions per packet (should decrease)
- Route stability (number of route changes per hour)
- Timeout rate on reliable packets (should decrease)
- Average path ETX vs actual delivery success rate (correlation)

---

## Conclusion

Implementing ETX-based routing will significantly improve path selection in the LoRaMesher network, especially for large deployments (30+ nodes). The proposed solution:

1. **Minimal changes**: ~150 lines of code, 4 bytes per route entry
2. **No protocol breaking**: Uses existing metric field
3. **Handles real-world conditions**: Adapts to varying link quality
4. **Simple Phase 1 approach**: Assume symmetry (good for LoRa)
5. **Clear upgrade path**: Can add bidirectional ETX if asymmetry is problematic

The asymmetric link analysis shows that for typical LoRa networks with similar hardware, assuming link symmetry is acceptable for Phase 1. If field testing reveals significant asymmetry, Phase 2 (maximum ETX) can be implemented without protocol changes.

---

## References

1. De Couto, D. S., Aguayo, D., Bicket, J., & Morris, R. (2003). "A high-throughput path metric for multi-hop wireless routing." *MobiCom '03*.
2. RFC 6298: "Computing TCP's Retransmission Timer" (referenced for SRTT/RTTVAR)
3. LoRaMesher library documentation and source code analysis
4. Current implementation files:
   - `RouteNode.h` - Route table entry structure
   - `NetworkNode.h` - Network node advertisement structure
   - `RoutingTableService.cpp` - Routing algorithm implementation
   - `LoraMesher.cpp` - Hello packet transmission

---

**Document Version**: 1.0
**Last Updated**: 2025-10-17
