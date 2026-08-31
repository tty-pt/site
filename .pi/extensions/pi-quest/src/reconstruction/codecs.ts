export function coerceString(v: any): string | undefined {
	return typeof v === "string" ? v : undefined;
}

export function coerceStringOrNull(v: any): string | null {
	return typeof v === "string" ? v : null;
}

export function coerceNumber(v: any, def = 0): number {
	return typeof v === "number" ? v : def;
}

export function coerceBoolean(v: any, def = false): boolean {
	return typeof v === "boolean" ? v : def;
}

export function coerceArray<T>(v: any, fallback: T[] = []): T[] {
	return Array.isArray(v) ? v : fallback;
}

export function coerceNullableNumber(v: any): number | undefined {
	return typeof v === "number" ? v : undefined;
}
