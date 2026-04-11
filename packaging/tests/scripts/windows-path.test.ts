import { describe, expect, it, vi } from "vitest";
import { detectWsl, toWindowsPath } from "../../scripts/windows-path.js";

describe("windows-path", () => {
	it("detectWsl reads proc version", () => {
		const fsLike = {
			readFileSync: vi.fn().mockReturnValue("Linux version ... Microsoft ..."),
			existsSync: vi.fn().mockReturnValue(false),
		};

		expect(detectWsl("/proc/version", "/nope", fsLike)).toBe(true);
	});

	it("toWindowsPath returns original path outside WSL", () => {
		expect(toWindowsPath("/tmp/demo", { isWsl: false })).toBe("/tmp/demo");
	});

	it("toWindowsPath caches converted paths", () => {
		const execFileSyncFn = vi.fn().mockReturnValue("C:\\tmp\\demo\n");
		const cache = new Map<string, string>();

		expect(toWindowsPath("/tmp/demo", { isWsl: true, execFileSyncFn, cache })).toBe("C:\\tmp\\demo");
		expect(toWindowsPath("/tmp/demo", { isWsl: true, execFileSyncFn, cache })).toBe("C:\\tmp\\demo");
		expect(execFileSyncFn).toHaveBeenCalledTimes(1);
	});

	it("toWindowsPath preserves wsl.localhost UNC paths from wslpath", () => {
		const execFileSyncFn = vi.fn().mockReturnValue("\\\\wsl.localhost\\Ubuntu\\home\\demo\n");

		expect(toWindowsPath("/home/demo", { isWsl: true, execFileSyncFn })).toBe("\\\\wsl.localhost\\Ubuntu\\home\\demo");
	});
});
