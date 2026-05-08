import { defineConfig } from "vitest/config";

export default defineConfig({
	test: {
		include: [
			"packaging/tests/**/*.test.ts",
			"tools/**/*.test.ts",
		],
	},
});
