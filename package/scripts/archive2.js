const fs = require("fs-extra");
const path = require("path");
const { exec } = require("child_process");

const { archive2Path, buildTempDir } = require("./properties");

const commonSourceDir = path.join(buildTempDir, "files");

const productMainDir = path.join(commonSourceDir, "product", ".archive-main");
const productArchiveDir = path.join(commonSourceDir, "product", "archive");
const productArchivePath = path.join(productArchiveDir, "Lootman - Main.ba2");
const productArchiveCmd = `"${archive2Path}" "${productMainDir}" -r="${productMainDir}" -c="${productArchivePath}"`;

fs.removeSync(productArchiveDir);
fs.mkdirsSync(productArchiveDir);
exec(productArchiveCmd, (error, stdout, stderr) => {
    if (error) {
        return console.error(error);
    }
    console.log(stdout);
    console.log(stderr);
});
