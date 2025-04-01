#!/bin/bash

# Run the command in the background
./cache_simulator test_4 &

# Get the process ID of the last background command
PID=$!

# Wait for 3 seconds
sleep 2

# Terminate the process
kill -INT $PID

echo "Run 1"
echo "diffing core_0_output.txt"
diff myTest4/core_0_output.txt tests/test_4/run_1/core_0_output.txt
echo "diffing core_1_output.txt"
diff myTest4/core_1_output.txt tests/test_4/run_1/core_1_output.txt
echo "diffing core_2_output.txt"
diff myTest4/core_2_output.txt tests/test_4/run_1/core_2_output.txt
echo "diffing core_3_output.txt"
diff myTest4/core_3_output.txt tests/test_4/run_1/core_3_output.txt

echo "Run 2"
echo "diffing core_0_output.txt"
diff myTest4/core_0_output.txt tests/test_4/run_2/core_0_output.txt
echo "diffing core_1_output.txt"
diff myTest4/core_1_output.txt tests/test_4/run_2/core_1_output.txt
echo "diffing core_2_output.txt"
diff myTest4/core_2_output.txt tests/test_4/run_2/core_2_output.txt
echo "diffing core_3_output.txt"
diff myTest4/core_3_output.txt tests/test_4/run_2/core_3_output.txt

echo "Run 3"
echo "diffing core_0_output.txt"
diff myTest4/core_0_output.txt tests/test_4/run_3/core_0_output.txt
echo "diffing core_1_output.txt"
diff myTest4/core_1_output.txt tests/test_4/run_3/core_1_output.txt
echo "diffing core_2_output.txt"
diff myTest4/core_2_output.txt tests/test_4/run_3/core_2_output.txt
echo "diffing core_3_output.txt"
diff myTest4/core_3_output.txt tests/test_4/run_3/core_3_output.txt

echo "Run 4"
echo "diffing core_0_output.txt"
diff myTest4/core_0_output.txt tests/test_4/run_4/core_0_output.txt
echo "diffing core_1_output.txt"
diff myTest4/core_1_output.txt tests/test_4/run_4/core_1_output.txt
echo "diffing core_2_output.txt"
diff myTest4/core_2_output.txt tests/test_4/run_4/core_2_output.txt
echo "diffing core_3_output.txt"
diff myTest4/core_3_output.txt tests/test_4/run_4/core_3_output.txt
