#!/bin/sh
set -e

echo "========================================="
echo "Running Integration Tests"
echo "========================================="

failed=0

# Run each test script in tests/integration/, but not run_all.sh itself
for test in tests/integration/[0-9]*.sh; do
    if [ -x "$test" ]; then
        echo ""
        echo "Running: $test"
        echo "-----------------------------------------"
        if "$test"; then
            echo "PASS: $test"
        else
            echo "FAIL: $test"
            failed=1
        fi
    fi
done

echo ""
echo "========================================="
if [ $failed -eq 0 ]; then
    echo "All integration tests passed!"
else
    echo "Some integration tests failed!"
fi
echo "========================================="

exit $failed
