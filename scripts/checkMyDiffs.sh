#!/bin/bash

# Check tests 1 and 2 (single run)
for test_num in 1 2; do
    echo "Checking diff for test_${test_num}..."
    
    # For each processor core (0-3)
    for core_num in 0 1 2 3; do
        echo "  Comparing core_${core_num}_output.txt..."
        diff -q "myTest${test_num}/core_${core_num}_output.txt" "tests/test_${test_num}/core_${core_num}_output.txt"
        if [ $? -eq 0 ]; then
            echo "  Core ${core_num} passed!"
        else
            echo "  Core ${core_num} FAILED!"
        fi
    done
done

