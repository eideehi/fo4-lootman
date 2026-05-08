export const projectRoot: string;

export function normalizeCommand(command: string): string;
export function describeUserCommand(command: string): string;
export function assertProjectExecutionContext(command: string, cwd?: string, root?: string): void;
export function findToolArtifacts(
	tool: string,
	root?: string,
	platform?: string,
): { packageJson: string; shimPaths: string[]; hasPackage: boolean; hasShim: boolean };
export function assertRequiredTools(command: string, root?: string, platform?: string): void;

export interface PreflightOptions {
	projectRoot?: string;
	cwd?: string;
	platform?: string;
}

export function runPreflight(command: string, options?: PreflightOptions): void;
