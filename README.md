# How to run:

1. C++17 - C++20 is required
2. Makefile is self (student) created. It contains the following commands:
      - make          <-- This command is to create the "simplify" executable
      - make clean    <-- This command is to clean up the executable
      - make rebuild  <-- This command is to rebuild the executable

Within the git repo, the Input and Expected Output test cases have already been uploaded.
Therefore, after running the "make" command, run the function

# The folder path to the correct folder for testing is currently hardcoded to look into the "Input_Files" folder
# Hardcoded line can be found in line 391 of the read_csv function
3. ./simplify <input.csv> <target_vertices>
