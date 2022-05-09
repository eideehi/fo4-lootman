const fs = require("fs-extra");
const path = require("path");

const { resourcesRoot, buildTempDir } = require("./properties");
const { replace } = require("./replace-text-in-file");

const fomodConfigDir = path.join(resourcesRoot, "fomod");
const outDir = path.join(buildTempDir, "fomod");

fs.copySync(fomodConfigDir, outDir);

replace(path.join(outDir, "info.xml"), new Array({
    key: "__MOD_VERSION__",
    value: process.env.npm_package_version
}));
