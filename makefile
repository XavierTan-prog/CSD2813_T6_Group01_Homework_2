#--------------- MAKEFILE VERSION 80085.69.420.TreefiddyFive ---------------#
# READ ME:
# To use the makefile for Homework 2 for Data Struct, here are the use cases:
# DO BE REMINDED THAT VALGRIND IS NOT REQUIRED FOR THIS ASS
# Make sure to type this in WSL in the file path where the files are found
# Read the comments to further understand wtf is happening just in case

# make							(Creates running file)
# make clean         			(Removes any and all object files and .out files)
# make rebuild         			(Rebuilds the executable)

#                                                                                      \
Ignore this but if you curious:                                                         \
Phony targets are names that represent commands to be executed,                          \
rather than actual files that make should create or update.                               \
Declaring it as .PHONY explicitly tells make that it's a command,                          \
preventing make from looking for a file with that name or considering its modification time \
#
.PHONY: clean rebuild

# Source files
DRIVER_C = main.cpp

# Output executable file
TEST_OUT = simplify

# Compiler and flags (For C++)
CXX_COMPILER = g++
CXXFLAGS = -std=c++17 -pedantic -Wall -Wextra -Werror -Wconversion

# Default target
all: $(TEST_OUT)

# Rule to build the target executable
$(TEST_OUT): $(DRIVER_C)
	$(CXX_COMPILER) $(CXXFLAGS) $(DRIVER_C) -o $(TEST_OUT)

#------------------------------------------------------------------------------#
# REMOVES ALL FILES SPWANED FROM MAKEFILE
clean:
	rm -f $(TEST_OUT)
#------------------------------------------------------------------------------#

# Rebuild
#--------------- !!!WARNING!!! ---------------#
# Using make rebuild command will rebuild the ENTIRE executable
#--------------- !!!WARNING!!! ---------------#
rebuild:
	clean