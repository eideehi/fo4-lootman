const fs = require("fs-extra");

const { buildTempRoot } = require("./properties");

fs.removeSync(buildTempRoot);