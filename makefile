#--------------- MAKEFILE VERSION 80085.69.420.TreefiddyFive ---------------#
# READ ME:
# To use the makefile for Homework 2 for Data Struct, here are the use cases:
# DO BE REMINDED THAT VALGRIND IS NOT REQUIRED FOR THIS ASS
# Make sure to type this in WSL in the file path where the files are found
# Read the comments to further understand wtf is happening just in case

# make all						(All except Valgrind testing)
# make check					(Checks your code for illegal stuff [Drugs])
# make build					(Compiles your code)
# make [one1-one3]				(Runs all tests within the ./one folder)
# make [two1-two3]				(Runs all tests within the ./two folder)
# make clean         			(Removes any and all object files and .out files)
# make rebuild       			(Cleans everything and recompiles for all tests)

#                                                                                      \
Ignore this but if you curious:                                                         \
Phony targets are names that represent commands to be executed,                          \
rather than actual files that make should create or update.                               \
Declaring it as .PHONY explicitly tells make that it's a command,                          \
preventing make from looking for a file with that name or considering its modification time \
#
.PHONY: all

# Source files
DRIVER_C = driver-oop.cpp
INPUT_CHXX = process.cpp student.cpp grad.cpp undergrad.cpp
ALLFILES_C = $(DRIVER_C) $(INPUT_CHXX)

# Test files
ONETEST = ./one/students1.txt
TWOTEST = ./two/students2.txt

TEST_ONE = ./

# Output executable file
TEST_OUT = test.out

# Compared to your "correct output" file
STUDENTOUT_TXT = student-output.txt
EXPECTED_ONE_TXT = ./one/out
EXPECTED_TWO_TXT = ./two/out

# Compiler and flags (For C++)
CXX_COMPILER = g++
CXXFLAGS = -std=c++17 -pedantic -Wall -Wextra -Werror -Wconversion

# Debuggging Options
VALGRIND_OPTIONS = -q --leak-check=full
DIFF_OPTIONS = -y --strip-trailing-cr --suppress-common-lines

#------------------------------------------------------------------------------#
# Build command for doing all except Valgrind testing:
all: check build testone1 testone2 testone3 testtwo1 testtwo2 testtwo3

# -H flag added to grep — ensures the file name is printed in the output (even if multiple files are searched)
#------------------------------------------------------------------------------#
# Syntax restrictions check for files
check:
	@echo "🔍 Checking all files for forbidden keywords..."

	# Forbid usage of 'new' and 'delete' in specific files
	@if grep -Hn --color=always -E '\b(new|delete)\b' process.cpp student.cpp grad.cpp undergrad.cpp >/dev/null; then \
		echo "❌ Forbidden keywords ('new' or 'delete') detected:"; \
		grep -Hn --color=always -E '\b(new|delete)\b' process.cpp student.cpp grad.cpp undergrad.cpp; \
		exit 1; \
	fi

	@echo "✅ Syntax check passed."
#------------------------------------------------------------------------------#


build:
	$(CXX_COMPILER) $(CXXFLAGS) $(ALLFILES_C) -o $(TEST_OUT)

#------------------------------------------------------------------------------#
# Build command for testing:
one1 one2 one3:
	@echo "running $@"
	./$(TEST_OUT) $(ONETEST) $(subst one,,$@) > student-output-$@
	@diff $(EXPECTED_ONE_TXT)$(subst one,,$@) student-output-$@ $(DIFF_OPTIONS) > diffresult$@ || true
	@if diff -q $(EXPECTED_ONE_TXT)$(subst one,,$@) student-output-$@ --strip-trailing-cr > /dev/null; then \
		echo "✅ test$@ passed"; \
	else \
		echo "❌ $@ failed (see diffresult$@ for details)"; \
	fi
#------------------------------------------------------------------------------#

#------------------------------------------------------------------------------#
# Build command for testing:
two1 two2 two3:
	@echo "running $@"
	./$(TEST_OUT) $(TWOTEST) $(subst two,,$@) > student-output-$@
	@diff $(EXPECTED_TWO_TXT)$(subst two,,$@) student-output-$@ $(DIFF_OPTIONS) > diffresult$@ || true
	@if diff -q $(EXPECTED_TWO_TXT)$(subst two,,$@) student-output-$@ --strip-trailing-cr > /dev/null; then \
		echo "✅ test$@ passed"; \
	else \
		echo "❌ $@ failed (see diffresult$@ for details)"; \
	fi
#------------------------------------------------------------------------------#


# The following 3 tests is for folder 'one'
#------------------------------------------------------------------------------#
# Build command for ./one/out1:
testone1:
	$(MAKE) one1
#------------------------------------------------------------------------------#

#------------------------------------------------------------------------------#
# Build command for ./one/out2:
testone2:
	$(MAKE) one2
#------------------------------------------------------------------------------#

#------------------------------------------------------------------------------#
# Build command for ./one/out3:
testone3:
	$(MAKE) one3
#------------------------------------------------------------------------------#

# The following 3 tests is for folder 'two'
#------------------------------------------------------------------------------#
# Build command for ./two/out1:
testtwo1:
	$(MAKE) two1
#------------------------------------------------------------------------------#

#------------------------------------------------------------------------------#
# Build command for ./two/out2:
testtwo2:
	$(MAKE) two2
#------------------------------------------------------------------------------#

#------------------------------------------------------------------------------#
# Build command for ./two/out3:
testtwo3:
	$(MAKE) two3
#------------------------------------------------------------------------------#


#------------------------------------------------------------------------------#
# REMOVES ALL FILES SPWANED FROM MAKEFILE
clean:
	rm -f $(TEST_OUT) student-output-* diffresult* difference* $(STUDENTOUT_TXT)
#------------------------------------------------------------------------------#

# Rebuild
#--------------- !!!WARNING!!! ---------------#
# Using make rebuild command will rebuild ALL tests of 0 - 3
# If you dont wanna rebuild all for compile and run all tests, 
# use other make commands
#--------------- !!!WARNING!!! ---------------#
rebuild:
	clean all