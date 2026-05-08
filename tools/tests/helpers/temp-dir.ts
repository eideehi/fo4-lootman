import fs from "fs-extra";
import os from "node:os";
import path from "node:path";

export function createTempDir(prefix = "lootman-packaging-test-"): string {
	return fs.mkdtempSync(path.join(os.tmpdir(), prefix));
}

export function removeTempDir(dir: string): void {
	fs.removeSync(dir);
}
