import re
from collections import defaultdict


def parse_log_file(filename):
    # Regular expression to match the log pattern and capture the tag, function, and unused stack space
    log_pattern = re.compile(
        r"\d{2}:\d{2}:\d{2}\.\d{3} > \[\s*\d+\]\[\w+\]\[(.+)\.cpp:(\d+)] (\w+)\(\): \[(.+)\] Stack space unused after entering the task: (\d+)"
    )

    # Dictionary to hold the stack space info, organized by tag and function
    stack_space_by_tag_function = defaultdict(list)

    # Read the file
    with open(filename, "r") as file:
        for line in file:
            # Match the pattern
            match = log_pattern.match(line)
            if match:
                # Extract the tag, function name, line number, and unused stack space
                tag = match.group(4)
                function = match.group(3)
                line_number = match.group(2)
                unused_stack_space = int(match.group(5))

                # Append the unused stack space to the tag and function's list
                stack_space_by_tag_function[(tag, function, line_number)].append(
                    unused_stack_space
                )

    return stack_space_by_tag_function


def main():
    filename = input("Enter the log filename: ")
    stack_space_data = parse_log_file(filename)

    # Sort the stack space data by tag and then function name (and line number for tie-breaking)
    sorted_tag_functions = sorted(
        stack_space_data.keys(), key=lambda x: (x[0], x[1], int(x[2]))
    )

    # Print the stack space information organized by tag and function
    for tag_function_info in sorted_tag_functions:
        stack_spaces = stack_space_data[tag_function_info]
        tag, function, line_number = tag_function_info

        stack_spaces_num = len(stack_spaces)

        # Calculate the average stack space
        average_stack_space = sum(stack_spaces) / stack_spaces_num

        # If stack_spaces is bigger than 10, print the last 10 elements
        if stack_spaces_num > 15:
            stack_spaces = stack_spaces[-15:]

        print(
            f"Tag {tag}, Function {function} (line {line_number}) (num {stack_spaces_num}) (average {average_stack_space}) has the following stack spaces unused: {stack_spaces}"
        )


if __name__ == "__main__":
    main()
