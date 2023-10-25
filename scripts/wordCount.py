import os


def count_words_in_file(file_path):
    with open(file_path, "r") as file:
        contents = file.read()
        words = contents.split()
        return len(words)


def count_words_in_dir(dir_path):
    total_word_count = 0
    for root, dirs, files in os.walk(dir_path):
        for file in files:
            file_path = os.path.join(root, file)
            total_word_count += count_words_in_file(file_path)
        for dirs in dirs:
            total_word_count += count_words_in_dir(dirs)
    return total_word_count


dir_path = (
    ".\.pio\libdeps\esp-wrover-kit\LoRaMesher\src"  # Change this to your directory path
)
print(f"Total word count: {count_words_in_dir(dir_path)}")
