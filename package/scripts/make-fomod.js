require("dotenv").config();

if (process.env.SEVENZIP_PATH === undefined) {
    console.error("SEVENZIP_PATH is not defined");
    process.exit(1);
}

const fs = require("fs-extra");
const path = require("path");
const { exec } = require("child_process");

const { archiveName, buildTempDir } = require("./properties");
const outDir = path.join(process.cwd(), "dist");
const outFile = path.join(outDir, `${archiveName}.7z`);

fs.mkdirsSync(outDir);
if (fs.existsSync(outFile)) {
    fs.removeSync(outFile);
}

const compressCmd = `"${process.env.SEVENZIP_PATH}" a "${outFile}" "${buildTempDir}/*"`;
exec(compressCmd,
    (error, stdout, stderr) => {
        if (error) {
            return console.error(error);
        }
        console.log(stdout);
        console.log(stderr);
    })
    .on("close", () => {
        console.log("The fomod archive has been maked.");
    });
