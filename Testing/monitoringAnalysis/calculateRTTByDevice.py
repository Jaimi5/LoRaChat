import re


def extract_rtt_values_final_pattern(filename):
    with open(filename, "r") as file:
        content = file.readlines()

    # Final regular expression to extract RTT, SRTT, and RTTVAR values with parentheses
    pattern = re.compile(r"Updating RTT \((\d+) ms\), SRTT \((\d+)\), RTTVAR \((\d+)\)")

    rtt_values, srtt_values, rttvar_values = [], [], []

    for line in content:
        match = pattern.search(line)
        if match:
            rtt_values.append(int(match.group(1)))
            srtt_values.append(int(match.group(2)))
            rttvar_values.append(int(match.group(3)))

    return rtt_values, srtt_values, rttvar_values
