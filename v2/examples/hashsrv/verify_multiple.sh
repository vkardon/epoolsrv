#!/bin/bash
# Thu Apr 23 05:14:05 PM PDT 2026
# By vkardon

# ==============================================================================
# DESCRIPTION:
#   Validates the server's data framing by comparing a line-by-line baseline 
#   against a high-speed raw stream.
#
# USAGE:
#   ./verify_multiple.sh <test_file>
# ==============================================================================

PORT=""
DATA_FILE=""
REF_SCRIPT="./verify_individual.sh"
TEMP_BASELINE="baseline.tmp"
TEMP_STREAM="stream.tmp"

PASS_ICON="\033[1;37;42m ✓ \033[0m" # Bold White Text on Green Background
FAIL_ICON="\033[1;37;41m ✘ \033[0m" # Bold White Text on Red Background

# Function to display usage
usage() 
{
    echo "Usage: $0 -p <port> -f <input_file>"
    echo "  -p : Port number of the server"
    echo "  -f : Path to input data file"
    exit 1
}

# Parse flags
while getopts "f:p:o:" opt; do
  case $opt in
    f) DATA_FILE="$OPTARG" ;;
    p) PORT="$OPTARG" ;;
    *) usage ;;
  esac
done

# Mandatory port check 
if [[ -z "$PORT" ]]; then
    echo -e "${FAIL_ICON} Error: Port is mandatory."
    usage
fi

# Mandatory input data file check
if [ -z "$DATA_FILE" ]; then
    echo -e "${FAIL_ICON} Error: No input file provided."
    usage
fi

# Input File Existence Check
if [ ! -f "$DATA_FILE" ]; then
    echo -e "${FAIL_ICON} Error: File '$DATA_FILE' not found."
    exit 1
fi

if [ ! -x "$REF_SCRIPT" ]; then
    echo -e "${FAIL_ICON} Error: Reference script '$REF_SCRIPT' not found or not executable."
    exit 1
fi

echo "Starting Chunking Integrity Test: $DATA_FILE ..."

# Generate Baseline
# Run the script, send all output (stdout and stderr) to /dev/null
echo "1: Generating reference hashes (Individual Connections)..."
$REF_SCRIPT -p "$PORT" -f "$DATA_FILE" -o "$TEMP_BASELINE" > /dev/null 2>&1

# Check the exit status of the previous command
if [ $? -ne 0 ]; then
    echo -e "${FAIL_ICON} Error: Baseline generation failed!"
    echo "Aborting test."
    rm -f "$TEMP_BASELINE"
    exit 1
fi

echo -e "${PASS_ICON} Baseline generated successfully."

# The Stream Test
echo "2: Sending raw stream (Single Connection)..."
nc localhost "$PORT" < "$DATA_FILE" > "$TEMP_STREAM"

# Check if nc failed to connect or crashed
if [ $? -ne 0 ]; then
    echo -e "${FAIL_ICON} Error: Netcat failed to communicate with the server."
    rm -f "$TEMP_BASELINE" "$TEMP_STREAM"
    exit 1
fi

echo -e "${PASS_ICON} Stream transfer complete. Received $(wc -l < "$TEMP_STREAM") hashes."
 
# Comparison
echo "3: Comparing data framing..."

if diff "$TEMP_BASELINE" "$TEMP_STREAM" > /dev/null; then
    echo -e "${PASS_ICON} SUCCESS: Stream matches baseline."
    #echo "    Server correctly identifies message boundaries in the stream."
    rm -f "$TEMP_BASELINE" "$TEMP_STREAM"
    exit 0
else
    echo -e "${FAIL_ICON} FAILURE: Chunking mismatch!"
    echo "    The server smushed or misparsed the data stream."
    echo ""
    echo "SIDE-BY-SIDE (Left: Individual | Right: Stream)"
    #diff -y "$TEMP_BASELINE" "$TEMP_STREAM"
    diff --side-by-side --suppress-common-lines "$TEMP_BASELINE" "$TEMP_STREAM"
    rm -f "$TEMP_BASELINE" "$TEMP_STREAM"
    exit 1
fi

