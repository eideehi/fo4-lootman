import { afterEach, describe, expect, it, vi } from "vitest";
import { createFileProgress, formatElapsedMs, runWhile } from "../../scripts/progress.js";

function createStream(isTTY: boolean) {
	const writes: string[] = [];
	return {
		isTTY,
		writes,
		write: vi.fn((chunk: string) => {
			writes.push(chunk);
			return true;
		}),
	};
}

describe("progress", () => {
	afterEach(() => {
		vi.useRealTimers();
		vi.restoreAllMocks();
	});

	it("renders determinate progress in TTY mode", () => {
		const stream = createStream(true);
		const progress = createFileProgress(2, "Syncing files", { stream });

		progress.advance();
		progress.finish();

		expect(stream.write).toHaveBeenCalled();
		expect(stream.writes.join("")).toContain("Syncing files");
		expect(stream.writes.join("")).toContain("2/2");
	});

	it("does not render determinate progress for zero totals", () => {
		const stream = createStream(true);
		const progress = createFileProgress(0, "Syncing files", { stream });

		progress.advance();
		progress.finish();

		expect(stream.write).not.toHaveBeenCalled();
	});

	it("runs without a spinner when the stream is not interactive", async () => {
		const stream = createStream(false);
		const createSpinner = vi.fn();

		const result = await runWhile("Waiting", async () => "done", {
			createSpinner,
			stream,
		});

		expect(result).toBe("done");
		expect(createSpinner).not.toHaveBeenCalled();
	});

	it("cleans up the spinner after a successful wait", async () => {
		vi.useFakeTimers();
		const stream = createStream(true);
		let now = 0;
		let resolveFn: ((value: string) => void) | null = null;
		const spinner = {
			text: "",
			stop: vi.fn(),
		};

		const task = runWhile("Waiting", () => new Promise<string>((resolve) => {
			resolveFn = resolve;
		}), {
			createSpinner: (text) => {
				spinner.text = text;
				return spinner;
			},
			now: () => now,
			stream,
		});

		now = 2_000;
		await vi.advanceTimersByTimeAsync(2_000);
		expect(spinner.text).toBe("Waiting (2s)");

		resolveFn!("done");
		await task;

		expect(spinner.stop).toHaveBeenCalledOnce();
	});

	it("cleans up the spinner after a failed wait", async () => {
		vi.useFakeTimers();
		const stream = createStream(true);
		const spinner = {
			text: "",
			stop: vi.fn(),
		};

		await expect(runWhile("Waiting", async () => {
			throw new Error("boom");
		}, {
			createSpinner: (text) => {
				spinner.text = text;
				return spinner;
			},
			stream,
		})).rejects.toThrow("boom");

		expect(spinner.stop).toHaveBeenCalledOnce();
	});

	it("formats elapsed durations compactly", () => {
		expect(formatElapsedMs(0)).toBe("0s");
		expect(formatElapsedMs(9_900)).toBe("9s");
		expect(formatElapsedMs(65_000)).toBe("1m 5s");
	});
});
