#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <nd/stone.h>

int main(void)
{
	int n[4] = { 0 };

	for (uint32_t i = 0; i < 4096; i++)
		n[stone_size_roll(i * 2654435761u >> 7)]++;
	assert(n[1] * 100 > 55 * 4096);                       /* mostly stones */
	assert(n[2] * 100 >= 20 * 4096 && n[2] * 100 <= 30 * 4096);
	assert(n[3] * 100 >= 8 * 4096 && n[3] * 100 <= 17 * 4096);
	assert(n[0] == 0);
	assert(n[1] > n[2] && n[2] > n[3]);
	printf("PASS: stone size roll is %d stones, %d rocks, %d boulders of 4096\n", n[1], n[2], n[3]);
	return 0;
}
