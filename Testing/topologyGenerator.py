"""
Topology Generator for ETX Testing

This module provides predefined network topologies with loss rate matrices
for systematic ETX routing validation.

Loss Rate Matrix Format:
- Values range from 0.0 (perfect link) to 1.0 (no connectivity)
- 0.0 = 0% packet loss (perfect connectivity)
- 0.5 = 50% packet loss
- 1.0 = 100% packet loss (no connectivity)

Matrix format includes headers in first row and column:
[
    ['', node1, node2, node3, ...],
    [node1, loss_11, loss_12, loss_13, ...],
    [node2, loss_21, loss_22, loss_23, ...],
    ...
]
"""

import numpy as np


class TopologyGenerator:
    """Generate predefined network topologies for ETX testing"""

    @staticmethod
    def scenario_1_triangle_of_trust():
        """
        Scenario 1: Triangle of Trust

        Objective: Compare ETX vs hop-count routing

        Topology:
            A ---[perfect]--- B ---[perfect]--- C
             \\                                 /
              -------[20% loss]---------------

        Expected behavior:
        - Hop-count: A→C direct (1 hop, but lossy)
        - ETX: A→B→C (2 hops, but reliable)

        Nodes: 3
        Loss rates:
        - A↔B: 0.0 (perfect)
        - B↔C: 0.0 (perfect)
        - A↔C: 0.2 (20% loss)
        """
        nodes = [1000, 2000, 3000]  # Node addresses
        n = len(nodes)

        # Initialize matrix with headers
        matrix = [[''] + nodes]

        # A↔B: perfect (0.0)
        # B↔C: perfect (0.0)
        # A↔C: 20% loss (0.2)
        loss_rates = [
            [0.0, 0.0, 0.2],  # From A to [A, B, C]
            [0.0, 0.0, 0.0],  # From B to [A, B, C]
            [0.2, 0.0, 0.0],  # From C to [A, B, C]
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_2_asymmetric_alley():
        """
        Scenario 2: Asymmetric Alley

        Objective: Test bidirectional ETX with asymmetric links

        Topology (loss rates shown as forward/reverse):
            A --[5%/40%]-- B --[5%/40%]-- C --[5%/40%]-- D

        Expected behavior:
        - Forward direction (A→D): Good quality, ETX ~3.15
        - Reverse direction (D→A): Poor quality, ETX ~5.33
        - Should select different routes in each direction

        Nodes: 4
        """
        nodes = [1000, 2000, 3000, 4000]
        n = len(nodes)

        matrix = [[''] + nodes]

        # Asymmetric links: forward 5%, reverse 40%
        loss_rates = [
            [0.0, 0.05, 1.0, 1.0],   # From A: A→B good
            [0.40, 0.0, 0.05, 1.0],  # From B: B→A poor, B→C good
            [1.0, 0.40, 0.0, 0.05],  # From C: C→B poor, C→D good
            [1.0, 1.0, 0.40, 0.0],   # From D: D→C poor
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_3_degrading_chain():
        """
        Scenario 3: Degrading Chain

        Objective: Test route selection with varying link quality

        Topology:
            A --[0%]-- B --[10%]-- C --[20%]-- D --[30%]-- E

        Expected behavior:
        - ETX increases along the chain
        - A→E total ETX should reflect cumulative degradation
        - Should handle graceful degradation

        Nodes: 5
        """
        nodes = [1000, 2000, 3000, 4000, 5000]

        matrix = [[''] + nodes]

        # Chain with increasing loss rates
        loss_rates = [
            [0.0, 0.0, 1.0, 1.0, 1.0],   # From A: A→B perfect
            [0.0, 0.0, 0.10, 1.0, 1.0],  # From B: B→C 10% loss
            [1.0, 0.10, 0.0, 0.20, 1.0], # From C: C→D 20% loss
            [1.0, 1.0, 0.20, 0.0, 0.30], # From D: D→E 30% loss
            [1.0, 1.0, 1.0, 0.30, 0.0],  # From E
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_4_star_with_bad_hub():
        """
        Scenario 4: Star with Bad Hub

        Objective: Test route selection around unreliable central node

        Topology:
                  B
                  |[40%]
            C --[5%]-- HUB --[5%]-- D
                  |[40%]
                  E

        Hub has 40% loss to B and E, but 5% to C and D
        Alternative routes: C↔D direct with 15% loss

        Expected behavior:
        - C→D should prefer direct route (ETX ~2.35) over hub route (ETX ~2.22)
        - B→E forced through hub despite poor quality

        Nodes: 5 (HUB=1000, B=2000, C=3000, D=4000, E=5000)
        """
        nodes = [1000, 2000, 3000, 4000, 5000]  # HUB, B, C, D, E

        matrix = [[''] + nodes]

        # Star topology with variable hub quality
        loss_rates = [
            [0.0, 0.40, 0.05, 0.05, 0.40],  # From HUB
            [0.40, 0.0, 1.0, 1.0, 1.0],     # From B: only to HUB
            [0.05, 1.0, 0.0, 0.15, 1.0],    # From C: to HUB and D
            [0.05, 1.0, 0.15, 0.0, 1.0],    # From D: to HUB and C
            [0.40, 1.0, 1.0, 1.0, 0.0],     # From E: only to HUB
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_5_flapping_test():
        """
        Scenario 5: Flapping Test

        Objective: Verify hysteresis prevents route flapping

        Topology:
            A --[Route1: 12%]-- B
            |                   |
            +--[Route2: 11%]----+

        Route1 ETX ≈ 22.7
        Route2 ETX ≈ 22.5
        Difference: 0.9% (below 10% hysteresis threshold)

        Expected behavior:
        - Should stick with initial route despite minor quality differences
        - No flapping between routes

        Note: This scenario requires intermediate nodes for true multi-hop testing.
        Simplified version with direct comparison.

        Nodes: 4 (A, B, R1_intermediate, R2_intermediate)
        """
        nodes = [1000, 2000, 3000, 4000]  # A, B, R1, R2

        matrix = [[''] + nodes]

        # Two similar-quality routes
        loss_rates = [
            [0.0, 1.0, 0.12, 0.11],  # From A: to R1 (12%), to R2 (11%)
            [1.0, 0.0, 0.12, 0.11],  # From B: to R1 (12%), to R2 (11%)
            [0.12, 0.12, 0.0, 1.0],  # From R1: completes Route1
            [0.11, 0.11, 1.0, 0.0],  # From R2: completes Route2
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_6_storm_trigger():
        """
        Scenario 6: Storm Trigger

        Objective: Test storm prevention and rate limiting

        Topology: Fully connected mesh of 6 nodes
            All links have 5% loss (good but not perfect)

        Expected behavior:
        - Initial convergence may trigger multiple updates
        - Storm prevention should suppress excessive updates
        - Rate limiting (5s global, 10s per-route) should engage
        - Exponential backoff on storm detection

        Metrics to validate:
        - triggeredUpdatesSent vs updatesSuppressed ratio
        - Backoff counter values
        - Time between triggered updates

        Nodes: 6 (fully connected mesh)
        """
        nodes = [1000, 2000, 3000, 4000, 5000, 6000]
        n = len(nodes)

        matrix = [[''] + nodes]

        # Fully connected mesh with 5% loss on all links
        loss_rate = 0.05

        for i in range(n):
            row = [nodes[i]]
            for j in range(n):
                if i == j:
                    row.append(0.0)  # No loss to self
                else:
                    row.append(loss_rate)  # 5% loss to all others
            matrix.append(row)

        return np.array(matrix, dtype=object)

    @staticmethod
    def scenario_7_loop_prevention():
        """
        Scenario 7: Loop Prevention Test

        Objective: Validate duplicate packet detection

        Topology: Square with redundant paths
            A ---[5%]--- B
            |            |
           [5%]        [5%]
            |            |
            D ---[5%]--- C

        Expected behavior:
        - Routing updates should propagate through all paths
        - Duplicate detection should prevent routing loops
        - Each node should process each routing update only once
        - duplicatesDetected counter should increment

        Test method:
        - Monitor logs for "Duplicate packet detected" messages
        - Verify routing table converges to stable state
        - Check duplicatesDetected statistic

        Nodes: 4 (square topology)
        """
        nodes = [1000, 2000, 3000, 4000]  # A, B, C, D

        matrix = [[''] + nodes]

        # Square topology with redundant paths
        loss_rates = [
            [0.0, 0.05, 1.0, 0.05],  # From A: to B and D
            [0.05, 0.0, 0.05, 1.0],  # From B: to A and C
            [1.0, 0.05, 0.0, 0.05],  # From C: to B and D
            [0.05, 1.0, 0.05, 0.0],  # From D: to A and C
        ]

        for i, node in enumerate(nodes):
            matrix.append([node] + loss_rates[i])

        return np.array(matrix, dtype=object)

    @staticmethod
    def get_all_scenarios():
        """
        Get all available test scenarios

        Returns:
            dict: Mapping of scenario names to their generator functions
        """
        return {
            "scenario_1_triangle_of_trust": TopologyGenerator.scenario_1_triangle_of_trust,
            "scenario_2_asymmetric_alley": TopologyGenerator.scenario_2_asymmetric_alley,
            "scenario_3_degrading_chain": TopologyGenerator.scenario_3_degrading_chain,
            "scenario_4_star_with_bad_hub": TopologyGenerator.scenario_4_star_with_bad_hub,
            "scenario_5_flapping_test": TopologyGenerator.scenario_5_flapping_test,
            "scenario_6_storm_trigger": TopologyGenerator.scenario_6_storm_trigger,
            "scenario_7_loop_prevention": TopologyGenerator.scenario_7_loop_prevention,
        }

    @staticmethod
    def print_scenario_info():
        """Print information about all available scenarios"""
        scenarios = {
            "Scenario 1: Triangle of Trust": {
                "Nodes": 3,
                "Objective": "ETX vs hop-count comparison",
                "Key Feature": "Direct lossy link vs reliable 2-hop path"
            },
            "Scenario 2: Asymmetric Alley": {
                "Nodes": 4,
                "Objective": "Bidirectional ETX validation",
                "Key Feature": "Asymmetric links (5% forward, 40% reverse)"
            },
            "Scenario 3: Degrading Chain": {
                "Nodes": 5,
                "Objective": "Graceful degradation handling",
                "Key Feature": "Increasing loss rates (0%, 10%, 20%, 30%)"
            },
            "Scenario 4: Star with Bad Hub": {
                "Nodes": 5,
                "Objective": "Route selection around unreliable hub",
                "Key Feature": "Variable hub quality with alternative routes"
            },
            "Scenario 5: Flapping Test": {
                "Nodes": 4,
                "Objective": "Hysteresis verification",
                "Key Feature": "Two routes with <10% ETX difference"
            },
            "Scenario 6: Storm Trigger": {
                "Nodes": 6,
                "Objective": "Storm prevention validation",
                "Key Feature": "Fully connected mesh triggers updates"
            },
            "Scenario 7: Loop Prevention": {
                "Nodes": 4,
                "Objective": "Duplicate detection validation",
                "Key Feature": "Square topology with redundant paths"
            }
        }

        print("\n" + "="*70)
        print("ETX TESTING SCENARIOS")
        print("="*70)

        for name, info in scenarios.items():
            print(f"\n{name}")
            print("-" * len(name))
            for key, value in info.items():
                print(f"  {key:15s}: {value}")

        print("\n" + "="*70)


def main():
    """Example usage and testing"""
    import json

    # Print scenario information
    TopologyGenerator.print_scenario_info()

    # Generate example scenario
    print("\n\nExample: Scenario 1 - Triangle of Trust")
    print("-" * 50)

    topology = TopologyGenerator.scenario_1_triangle_of_trust()

    print("\nLoss Rate Matrix:")
    print(topology)

    print("\n\nMatrix interpretation:")
    print("- topology[0]: Headers ['', 1000, 2000, 3000]")
    print("- topology[1]: From node 1000: [1000, 0.0, 0.0, 0.2]")
    print("  → 0% loss to node 2000, 20% loss to node 3000")
    print("\nThis matrix can be passed to ChangeConfigurationSerial.get_cpp_function()")


if __name__ == "__main__":
    main()
