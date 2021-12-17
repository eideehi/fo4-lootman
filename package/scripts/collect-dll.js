require("dotenv").config();

if (process.env.RUNTIME_VERSION === undefined) {
    console.error("RUNTIME_VERSION is not defined");
    process.exit(1);
}

const fs = require("fs-extra");
const path = require("path");

const { projectRoot, buildTempDir } = require("./properties");

const srcDirRoot = path.join(projectRoot, "f4se_plugin", process.env.npm_package_name, "x64");
const dllName = `${process.env.npm_package_name}.dll`;

const commonOutDir = path.join(buildTempDir, "files");
const debugOutDir = path.join(commonOutDir, "debug", "dll");
const productOutDir = path.join(commonOutDir, "product", "dll");

fs.removeSync(debugOutDir);
fs.removeSync(productOutDir);

fs.copySync(path.join(srcDirRoot, "Debug", dllName), path.join(debugOutDir, process.env.RUNTIME_VERSION, dllName));
fs.copySync(path.join(srcDirRoot, "Release", dllName), path.join(productOutDir, process.env.RUNTIME_VERSION, dllName));
