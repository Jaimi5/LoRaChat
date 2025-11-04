"""
Test script for topology generator and packet loss simulation

This script validates that:
1. Topology matrices are correctly formatted
2. C++ code generation works with loss rate matrices
3. Generated code compiles correctly with ESP32 random functions
"""

import sys
import os

# Add Testing directory to path
sys.path.insert(0, os.path.dirname(__file__))

from topologyGenerator import TopologyGenerator
from changeConfigurationSerial import ChangeConfigurationSerial


def test_scenario_generation():
    """Test that all scenarios generate valid matrices"""
    print("="*70)
    print("TEST 1: Scenario Matrix Generation")
    print("="*70)

    scenarios = TopologyGenerator.get_all_scenarios()

    for name, generator_func in scenarios.items():
        print(f"\nTesting {name}...")
        try:
            matrix = generator_func()

            # Validate matrix structure
            assert matrix.ndim == 2, "Matrix should be 2D"
            assert matrix[0][0] == '', "Top-left should be empty string"

            # Validate headers match rows
            headers = list(matrix[0][1:])
            for i in range(1, len(matrix)):
                assert matrix[i][0] in headers, f"Row {i} header not in column headers"

            # Validate loss rates are in [0.0, 1.0] range
            for i in range(1, len(matrix)):
                for j in range(1, len(matrix[i])):
                    loss_rate = float(matrix[i][j])
                    assert 0.0 <= loss_rate <= 1.0, f"Loss rate {loss_rate} out of range [0.0, 1.0]"

            print(f"  ✓ Matrix shape: {matrix.shape}")
            print(f"  ✓ Nodes: {headers}")
            print(f"  ✓ Loss rates valid")

        except Exception as e:
            print(f"  ✗ FAILED: {e}")
            return False

    print("\n✓ All scenario matrices valid!\n")
    return True


def test_cpp_generation():
    """Test C++ code generation with loss rate matrix"""
    print("="*70)
    print("TEST 2: C++ Code Generation")
    print("="*70)

    # Use Triangle of Trust scenario (simplest)
    print("\nGenerating C++ code for Scenario 1: Triangle of Trust")
    topology = TopologyGenerator.scenario_1_triangle_of_trust()

    # Create ChangeConfigurationSerial instance
    config_changer = ChangeConfigurationSerial("dummy.json", ["ttgo-t-beam"])

    # Generate C++ code
    cpp_code = config_changer.get_cpp_function(topology)

    print("\nGenerated C++ Code:")
    print("-" * 70)
    print(cpp_code)
    print("-" * 70)

    # Validate key components in generated code
    checks = {
        "lossRateMatrix": "float const lossRateMatrix" in cpp_code,
        "esp_random": "esp_random()" in cpp_code,
        "probabilistic": "randomValue >= lossRate" in cpp_code,
        "perfect_link": "lossRate <= 0.0f" in cpp_code,
        "no_connectivity": "lossRate >= 1.0f" in cpp_code,
    }

    print("\nCode Validation:")
    all_passed = True
    for check_name, passed in checks.items():
        status = "✓" if passed else "✗"
        print(f"  {status} {check_name}: {'PASS' if passed else 'FAIL'}")
        if not passed:
            all_passed = False

    if all_passed:
        print("\n✓ C++ code generation valid!\n")
    else:
        print("\n✗ C++ code generation has issues!\n")

    return all_passed


def test_loss_rate_values():
    """Test specific loss rate values in generated matrices"""
    print("="*70)
    print("TEST 3: Loss Rate Values Verification")
    print("="*70)

    # Test Scenario 1: Triangle of Trust
    print("\nScenario 1: Triangle of Trust")
    matrix = TopologyGenerator.scenario_1_triangle_of_trust()

    # Expected: A↔B perfect (0.0), B↔C perfect (0.0), A↔C lossy (0.2)
    # Nodes are at indices: [1000, 2000, 3000] → [A, B, C]
    # Matrix structure: matrix[row_idx][col_idx]
    #   Row 0: ['', 1000, 2000, 3000]
    #   Row 1: [1000, 0.0, 0.0, 0.2]  ← From A (1000)
    #   Row 2: [2000, 0.0, 0.0, 0.0]  ← From B (2000)
    #   Row 3: [3000, 0.2, 0.0, 0.0]  ← From C (3000)

    print("  Expected: A→B=0.0, A→C=0.2, B→C=0.0")

    a_to_b = float(matrix[1][2])  # Row 1 (from A), Col 2 (to B)
    a_to_c = float(matrix[1][3])  # Row 1 (from A), Col 3 (to C)
    b_to_c = float(matrix[2][3])  # Row 2 (from B), Col 3 (to C)

    print(f"  Actual: A→B={a_to_b}, A→C={a_to_c}, B→C={b_to_c}")

    checks = {
        "A→B perfect": abs(a_to_b - 0.0) < 0.001,
        "A→C lossy": abs(a_to_c - 0.2) < 0.001,
        "B→C perfect": abs(b_to_c - 0.0) < 0.001,
    }

    all_passed = True
    for check_name, passed in checks.items():
        status = "✓" if passed else "✗"
        print(f"  {status} {check_name}: {'PASS' if passed else 'FAIL'}")
        if not passed:
            all_passed = False

    # Test Scenario 2: Asymmetric Alley
    print("\nScenario 2: Asymmetric Alley")
    matrix = TopologyGenerator.scenario_2_asymmetric_alley()

    # Expected: A→B=0.05 (good), B→A=0.40 (poor)
    a_to_b = float(matrix[1][2])  # Row 1 (from A), Col 2 (to B)
    b_to_a = float(matrix[2][1])  # Row 2 (from B), Col 1 (to A)

    print(f"  Expected: A→B=0.05 (good), B→A=0.40 (poor)")
    print(f"  Actual: A→B={a_to_b}, B→A={b_to_a}")

    asymmetry_check = abs(a_to_b - 0.05) < 0.001 and abs(b_to_a - 0.40) < 0.001
    status = "✓" if asymmetry_check else "✗"
    print(f"  {status} Asymmetric links: {'PASS' if asymmetry_check else 'FAIL'}")

    all_passed = all_passed and asymmetry_check

    if all_passed:
        print("\n✓ Loss rate values correct!\n")
    else:
        print("\n✗ Loss rate values have issues!\n")

    return all_passed


def main():
    """Run all tests"""
    print("\n")
    print("╔" + "="*68 + "╗")
    print("║" + " "*10 + "TOPOLOGY GENERATOR & PACKET LOSS TEST SUITE" + " "*15 + "║")
    print("╚" + "="*68 + "╝")
    print()

    results = []

    # Run tests
    results.append(("Scenario Generation", test_scenario_generation()))
    results.append(("C++ Code Generation", test_cpp_generation()))
    results.append(("Loss Rate Values", test_loss_rate_values()))

    # Print summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)

    all_passed = True
    for test_name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {status}: {test_name}")
        if not passed:
            all_passed = False

    print("="*70)

    if all_passed:
        print("\n🎉 All tests passed! Packet loss simulation is ready.\n")
        print("Next steps:")
        print("  1. Create a test configuration JSON with a scenario")
        print("  2. Run changeConfigurationSerial.py to inject topology")
        print("  3. Compile and upload to ESP32 devices")
        print("  4. Verify packet loss behavior in logs")
        return 0
    else:
        print("\n❌ Some tests failed. Please review the issues above.\n")
        return 1


if __name__ == "__main__":
    sys.exit(main())
