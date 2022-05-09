const fs = require("fs-extra");

const { buildTempDir } = require("./properties");

fs.removeSync(buildTempDir);