#!/bin/bash

# Compile the cache simulator
echo "Building cache simulator..."
make

# Check if build was successful
if [ ! -f "./cache_simulator" ]; then
    echo "Error: Build failed or cache_simulator not found"
    exit 1
fi

# For each test case
for test_num in 1 2; do
    # Create directory if it doesn't exist
    test_dir="myTest${test_num}"
    echo "Creating directory ${test_dir}..."
    mkdir -p "${test_dir}"
    
    # Run the test in current directory
    echo "Running test_${test_num}..."
    timeout 5s ./cache_simulator "test_${test_num}"
    
    # Move the output files to the test directory
    echo "Moving output files to ${test_dir}..."
    mv core_*_output.txt "${test_dir}/" 2>/dev/null || echo "No output files found"
    
    echo "Completed test_${test_num}"
    echo "------------------------"
done

echo "All tests completed!"