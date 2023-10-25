import re


def get_free_heap_values(filename):
    with open(filename, "r", encoding="utf-8") as file:
        content = file.readlines()

    # Get the patter to find the free heap, ex: ...FREE HEAP: 1234 or ... Heap size: 1234
    pattern = re.compile(r".*(FREE HEAP|Heap size|Free ram|Free heap).* (\d+)")

    free_heap_values = []

    for line in content:
        match = pattern.search(line)
        if match:
            (
                heap_type,
                value,
            ) = match.groups()
            free_heap_values.append(int(value))

    return free_heap_values
