require("dotenv").config();

if (process.env.RUNTIME_VERSION === undefined) {
    console.error("RUNTIME_VERSION is not defined");
    process.exit(1);
}

const fs = require("fs-extra");
const path = require("path");

const { projectRoot, buildTempDir } = require("./properties");

const dllBuildDirRoot = path.join(projectRoot, "f4se_plugin", process.env.npm_package_name, "x64");
const dllName = `${process.env.npm_package_name}.dll`;

const dllDirRoot = path.join(buildTempDir, "files", "dll");
const debugOutDir = path.join(dllDirRoot, "debug");
const productOutDir = path.join(dllDirRoot, "product");

fs.removeSync(debugOutDir);
fs.removeSync(productOutDir);

fs.copySync(path.join(dllBuildDirRoot, "Debug", dllName), path.join(debugOutDir, process.env.RUNTIME_VERSION, dllName));
fs.copySync(path.join(dllBuildDirRoot, "Release", dllName), path.join(productOutDir, process.env.RUNTIME_VERSION, dllName));
