#!/bin/sh
# Integration tests for chords transpose API

set -e

BASE_URL="http://127.0.0.1:8080"
TEST_ID="test-transpose"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0

test_api() {
    local name="$1"
    local params="$2"
    local expected_pattern="$3"
    
    echo -n "Testing: $name ... "
    
    result=$(curl -s "${BASE_URL}/api/chords/transpose?id=${TEST_ID}&${params}")
    
    if echo "$result" | grep -q "$expected_pattern"; then
        echo "${GREEN}PASS${NC}"
        pass_count=$((pass_count + 1))
        return 0
    else
        echo "${RED}FAIL${NC}"
        echo "  Expected pattern: $expected_pattern"
        echo "  Got: $result"
        fail_count=$((fail_count + 1))
        return 1
    fi
}

echo "=== Chords Transpose API Integration Tests ==="
echo

# Test original (no transpose)
test_api "Original chords" "t=0" "^C"

# Test transpose up
test_api "Transpose +1" "t=1" "^C#"
test_api "Transpose +2" "t=2" "^D"
test_api "Transpose +11" "t=11" "^B"

# Test transpose down
test_api "Transpose -1" "t=-1" "^B"
test_api "Transpose -2" "t=-2" "^A#"
test_api "Transpose -11" "t=-11" "^C#"

# Test with flats (bemol)
test_api "Transpose +1 with flats" "t=1&b=1" "^Db"
test_api "Transpose +2 with flats" "t=2&b=1" "^D[^b#]"  # D without flat or sharp
test_api "Transpose -1 with flats" "t=-1&b=1" "^B[^b#]"  # B without flat or sharp
test_api "Transpose -2 with flats" "t=-2&b=1" "^Bb"

# Test with Latin notation
test_api "Original with Latin" "t=0&l=1" "^Do"
test_api "Transpose +2 with Latin" "t=2&l=1" "^Re"
test_api "Transpose +4 with Latin" "t=4&l=1" "^Mi"
test_api "Transpose -3 with Latin" "t=-3&l=1" "^La"

# Test combinations
test_api "Transpose +1 with flats and Latin" "t=1&b=1&l=1" "^Re.*b"
test_api "Transpose 0 flats and Latin" "t=0&b=1&l=1" "^Do[^#b]"

# Test that chord names are transposed correctly
test_api "Am transposed to Bm" "t=2" "Bm"
test_api "F transposed to G" "t=2" "G$"  # G at end of line

# Test that lyrics are preserved
test_api "Lyrics preserved" "t=2" "Amazing Grace"
test_api "Lyrics preserved 2" "t=5" "That saved a wretch"

echo
echo "=== Test Summary ==="
echo "${GREEN}Passed: $pass_count${NC}"
if [ $fail_count -gt 0 ]; then
    echo "${RED}Failed: $fail_count${NC}"
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
