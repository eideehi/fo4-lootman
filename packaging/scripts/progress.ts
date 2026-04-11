import ora from "ora";

export interface FileProgress {
	advance(amount?: number): void;
	finish(): void;
}

interface ProgressStream {
	isTTY?: boolean;
	write(chunk: string): boolean;
}

interface SpinnerLike {
	text: string;
	stop(): void;
}

interface ProgressOpts {
	now?: () => number;
	stream?: ProgressStream;
}

interface RunWhileOpts extends ProgressOpts {
	createSpinner?: (text: string, stream: ProgressStream) => SpinnerLike;
}

const BAR_WIDTH = 24;

function getStream(stream?: ProgressStream): ProgressStream {
	return stream ?? process.stderr;
}

function isInteractive(stream: ProgressStream): boolean {
	return stream.isTTY === true;
}

function clearLine(stream: ProgressStream): void {
	stream.write("\r\x1b[2K");
}

function renderBar(current: number, total: number): string {
	const safeTotal = Math.max(total, 1);
	const ratio = Math.max(0, Math.min(1, current / safeTotal));
	const filled = Math.round(ratio * BAR_WIDTH);
	return `[${"#".repeat(filled)}${"-".repeat(BAR_WIDTH - filled)}]`;
}

export function formatElapsedMs(ms: number): string {
	const totalSeconds = Math.max(0, Math.floor(ms / 1000));
	const minutes = Math.floor(totalSeconds / 60);
	const seconds = totalSeconds % 60;
	if (minutes > 0) {
		return `${minutes}m ${seconds}s`;
	}
	return `${seconds}s`;
}

export function createFileProgress(total: number, label: string, opts?: ProgressOpts): FileProgress {
	const stream = getStream(opts?.stream);
	if (total <= 0 || !isInteractive(stream)) {
		return {
			advance() {},
			finish() {},
		};
	}

	let current = 0;
	let finished = false;
	const render = (): void => {
		clearLine(stream);
		stream.write(`${label} ${renderBar(current, total)} ${current}/${total}`);
	};

	render();

	return {
		advance(amount = 1): void {
			if (finished) {
				return;
			}
			current = Math.min(total, current + amount);
			render();
		},
		finish(): void {
			if (finished) {
				return;
			}
			finished = true;
			current = total;
			render();
			stream.write("\n");
		},
	};
}

function defaultCreateSpinner(text: string, stream: ProgressStream): SpinnerLike {
	const spinner = ora({
		discardStdin: false,
		stream: stream as NodeJS.WriteStream,
		text,
	}).start();
	return spinner;
}

function stopSpinner(spinner: SpinnerLike): void {
	spinner.stop();
}

export async function runWhile<T>(message: string, fn: () => Promise<T> | T, opts?: RunWhileOpts): Promise<T> {
	const stream = getStream(opts?.stream);
	if (!isInteractive(stream)) {
		return await fn();
	}

	const now = opts?.now ?? Date.now;
	const createSpinner = opts?.createSpinner ?? defaultCreateSpinner;
	const startedAt = now();
	const spinner = createSpinner(`${message} (${formatElapsedMs(0)})`, stream);
	const timer = setInterval(() => {
		spinner.text = `${message} (${formatElapsedMs(now() - startedAt)})`;
	}, 1000);
	timer.unref?.();

	try {
		return await fn();
	} finally {
		clearInterval(timer);
		stopSpinner(spinner);
	}
}
