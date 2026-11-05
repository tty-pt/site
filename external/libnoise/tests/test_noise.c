#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <ttypt/noise.h>

int main(void)
{
	printf("=== Testing Standalone Generic N-Dimensional Noise Library ===\n");

	/* 1. Point sampling across 1D, 2D, 3D, 4D */
	{
		int v1 = noise_point_1d(10, 42, 4);
		int v2 = noise_point_2d(10, 20, 42, 4);
		int v3 = noise_point_3d(10, 20, 30, 42, 4);
		int v4 = noise_point_4d(10, 20, 30, 40, 42, 4);

		assert(v1 >= 0 && v1 <= 255);
		assert(v2 >= 0 && v2 <= 255);
		assert(v3 >= 0 && v3 <= 255);
		assert(v4 >= 0 && v4 <= 255);
		printf("PASS: generic point sampling (1D, 2D, 3D, 4D) within valid [0..255] bounds\n");
	}

	/* 2. Determinism of point queries */
	{
		for (int i = -10; i <= 10; i++) {
			int a = noise_point_3d(i, i * 2, i * 3, 99, 3);
			int b = noise_point_3d(i, i * 2, i * 3, 99, 3);
			assert(a == b);
		}
		printf("PASS: point sampling determinism verified\n");
	}

	/* 3. Smoothness / Continuity in 3D */
	{
		int prev = noise_point_3d(0, 0, 0, 1234, 4);
		for (int x = 1; x <= 64; x++) {
			int cur = noise_point_3d(x, 0, 0, 1234, 4);
			int diff = abs(cur - prev);
			assert(diff <= 40); /* Smooth step transitions */
			prev = cur;
		}
		printf("PASS: 3D point continuity verified\n");
	}

	/* 4. Matrix Stepper in 2D and 3D */
	{
		uint32_t m2d[1 << (2 * 4)]; // 16x16 = 256
		int16_t s2d[2] = { 0, 0 };
		octave_t oct2d[] = { { 4, 1 } };
		noise_oct(m2d, s2d, 1, oct2d, 55, 4, 2);
		assert(m2d[0] != 0 && m2d[255] != 0);

		uint32_t m3d[1 << (3 * 3)]; // 8x8x8 = 512
		int16_t s3d[3] = { 0, 0, 0 };
		octave_t oct3d[] = { { 3, 1 } };
		noise_oct(m3d, s3d, 1, oct3d, 55, 3, 3);
		assert(m3d[0] != 0 && m3d[511] != 0);
		printf("PASS: generic matrix stepping in 2D and 3D verified\n");
	}

	printf("=== ALL LIBNOISE TESTS PASSED ===\n");
	return 0;
}
