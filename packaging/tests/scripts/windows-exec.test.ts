import { describe, expect, it, vi } from "vitest";
import { runWindowsExe } from "../../scripts/windows-exec.js";

describe("windows-exec", () => {
	it("appends .exe to bare command names", async () => {
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "ok", stderr: "" });

		await runWindowsExe("xmake", ["build"], { cwd: "/tmp/project", execaFn });

		expect(execaFn).toHaveBeenCalledWith("xmake.exe", ["build"], {
			cwd: "/tmp/project",
			reject: false,
			stdio: "pipe",
		});
	});

	it("preserves explicit executable paths", async () => {
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "", stderr: "" });

		await runWindowsExe("C:\\tools\\7z.exe", ["a"], { execaFn, stdio: "inherit" });

		expect(execaFn).toHaveBeenCalledWith("C:\\tools\\7z.exe", ["a"], {
			cwd: undefined,
			reject: false,
			stdio: "inherit",
		});
	});

	it("wraps the command in cmd pushd when windowsCwd is provided", async () => {
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "", stderr: "" });

		await runWindowsExe("xmake", ["build", "-y"], {
			execaFn,
			stdio: "inherit",
			windowsCwd: "\\\\wsl.localhost\\distro\\project",
		});

		expect(execaFn).toHaveBeenCalledWith(
			"cmd.exe",
			[
				"/d",
				"/c",
				'pushd "\\\\wsl.localhost\\distro\\project" && "xmake.exe" "build" "-y"',
			],
			{
				cwd: "/mnt/c/Windows/System32",
				reject: false,
				stdio: "inherit",
			},
		);
	});

	it("uses cd /d for local Windows working directories", async () => {
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "", stderr: "" });

		await runWindowsExe("C:\\tools\\PapyrusCompiler.exe", ["C:\\tmp\\project.ppj"], {
			execaFn,
			stdio: "inherit",
			windowsCwd: "C:\\tmp\\project",
		});

		expect(execaFn).toHaveBeenCalledWith(
			"cmd.exe",
			[
				"/d",
				"/c",
				'cd /d "C:\\tmp\\project" && "C:\\tools\\PapyrusCompiler.exe" "C:\\tmp\\project.ppj"',
			],
			{
				cwd: "/mnt/c/Windows/System32",
				reject: false,
				stdio: "inherit",
			},
		);
	});

	it("can use powershell for local Windows working directories", async () => {
		const execaFn = vi.fn().mockResolvedValue({ exitCode: 0, stdout: "", stderr: "" });

		await runWindowsExe("C:\\tools\\Archive2.exe", ["C:\\tmp\\input", "-c=C:\\tmp\\out.ba2"], {
			execaFn,
			stdio: "inherit",
			windowsCwd: "C:\\tmp\\project",
			windowsShell: "powershell",
		});

		expect(execaFn).toHaveBeenCalledWith(
			"powershell.exe",
			[
				"-NoProfile",
				"-Command",
				"Set-Location -LiteralPath 'C:\\tmp\\project'; & 'C:\\tools\\Archive2.exe' 'C:\\tmp\\input' '-c=C:\\tmp\\out.ba2'; exit $LASTEXITCODE",
			],
			{
				cwd: "/mnt/c/Windows/System32",
				reject: false,
				stdio: "inherit",
			},
		);
	});
});
