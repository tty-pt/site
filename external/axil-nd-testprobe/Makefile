# Deliberately minimal, and deliberately NOT the house library shape: this
# fixture exists to prove an out-of-tree module loads when built
# outside the engine tree, so it should be as close to hand-written as a real
# one can get. Granted to the root region by soname in the warm-up below.
testprobe.so: testprobe.c
	$(CC) -shared -fPIC -I../axil-nd/include -I../nd-region/include -I../nd-world/include -I/usr/include \
		-o $@ $<
