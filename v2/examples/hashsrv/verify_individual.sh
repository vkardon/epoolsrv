#!/bin/bash
# Mon Apr 20 06:25:32 PM PDT 2026
# By vkardon

# ==============================================================================
# DESCRIPTION:
#   Validates SHA256 hashes from a server by processing lines individually.
#
# INPUT LOGIC:
#   1. Accepts a text file as the first argument.
#   2. If no file is provided, it automatically generates a temporary test file 
#      with 1000 random lines for demonstration purposes.
#
# NETWORK BEHAVIOR:
#   Opens a NEW TCP connection for each line. This ensures isolation and 
#   provides a 'Golden Standard' baseline for server response accuracy, 
#   bypassing potential streaming/buffering issues.
#
# VISUALS:
#   - [ ✓ ] (Green) : Hashes match.
#   - [ ✘ ] (Red)   : Mismatch detected; prints expected vs actual hash.
# ==============================================================================

# Default values
PORT=""
TEST_FILE=""
HASH_OUTPUT_FILE=""
IS_TEMPORARY=false

PASS_ICON="\033[1;37;42m ✓ \033[0m" # Bold White Text on Green Background
FAIL_ICON="\033[1;37;41m ✘ \033[0m" # Bold White Text on Red Background

# Function to display usage
usage() 
{
    echo "Usage: $0 -p <port> [-f <input_file>] [-o <output_file>]"
    echo "  -p : Port number of the server"
    echo "  -f : Optional path to test file (generates random data if omitted)"
    echo "  -o : Optional path to save raw hashes"
    exit 1
}

# Parse flags
while getopts "f:p:o:" opt; do
  case $opt in
    f) TEST_FILE="$OPTARG" ;;
    p) PORT="$OPTARG" ;;
    o) HASH_OUTPUT_FILE="$OPTARG" ;;
    *) usage ;;
  esac
done

# Mandatory Port Check 
if [[ -z "$PORT" ]]; then
    echo -e "${FAIL_ICON} Error: Port is mandatory."
    usage
fi

# If a hash output file is specified, clear it first
if [ -n "$HASH_OUTPUT_FILE" ]; then
    > "$HASH_OUTPUT_FILE"
fi

# Read a file specifeid from a command line or genereate default 
if [ -z "$TEST_FILE" ]; then
   # If no argument provided, generate a default test file
    TEST_FILE="auto_generated_test.txt"
    IS_TEMPORARY=true
    echo "No input file provided. Generating $TEST_FILE..."
    
    # Generate 1000 lines of random-ish data for testing
    for i in {1..1000}; do
        echo "Auto-generated test line $i with random ID $RANDOM"
    done > "$TEST_FILE"
elif [ ! -f "$TEST_FILE" ]; then
    # If an argument WAS provided but the file doesn't exist, exit
    echo -e ${FAIL_ICON} "Error: File '$TEST_FILE' not found!"
    exit 1
fi

echo "Starting File Test..."

LINE_NUM=0
FAILED=0
while IFS= read -r line || [ -n "$line" ]; do
    ((LINE_NUM++))

    # Calculate expected hash locally (no newline in the input string)
    EXPECTED=$(printf "%s" "$line" | sha256sum | awk '{print $1}')

    # Get hash from the server 
    ACTUAL=$(echo "$line" | nc localhost "$PORT" | tr -d '\r\n')

    # If we are saving hashes, write ACTUAL to the file
    if [ -n "$HASH_OUTPUT_FILE" ]; then
        echo "$ACTUAL" >> "$HASH_OUTPUT_FILE"
    fi

    # Compare
    if [ "$EXPECTED" == "$ACTUAL" ]; then
        #echo -e ${PASS_ICON} "Line $LINE_NUM: Match"
        echo -ne "\r${PASS_ICON} Validating line: $LINE_NUM"
    else
        echo -e ${FAIL_ICON} "Line $LINE_NUM: FAILED"
        echo "   Sent:     [$line]"
        echo "   Expected: [$EXPECTED]"
        echo "   Actual:   [$ACTUAL]"
        FAILED=1
        break # Exit on failure
    fi
done < "$TEST_FILE"

# If we didn't break out due to a failure, we need to move
# the cursor down to separate from "validating line" message
if [ "$FAILED" -eq 0 ]; then
    echo ""
fi

# Cleanup temporary file
if [ "$IS_TEMPORARY" = true ]; then
    #echo "Cleaning up temporary file: $TEST_FILE"
    rm -f "$TEST_FILE"
fi

# Report result
if [ $FAILED -eq 0 ]; then
    echo -e ${PASS_ICON} "Successfully validated $LINE_NUM lines!" >& 2
    exit 0 
else
    echo -e ${FAIL_ICON} "Validation stopped due to error." >& 2
    exit 1
fi

