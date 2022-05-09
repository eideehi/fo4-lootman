const fs = require("fs-extra");
const path = require("path");

const { resourcesRoot, buildTempDir } = require("./properties");

const commonFilesRoot = path.join(resourcesRoot, "lootman");
const outDir = path.join(buildTempDir, "files", "resources");

fs.removeSync(outDir)
fs.copySync(commonFilesRoot, outDir);
