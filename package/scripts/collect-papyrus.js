const fs = require("fs-extra");
const path = require("path");
const klaw = require("klaw");

const { projectRoot, buildTempDir } = require("./properties");

const commonOutDir = path.join(buildTempDir, "files");
const debugOutDir = path.join(commonOutDir, "debug", "papyrus-source");
const productOutDir = path.join(commonOutDir, "product", ".papyrus-source");

fs.removeSync(debugOutDir);
fs.removeSync(productOutDir);

const srcDirRoot = path.join(projectRoot, "papyrus", "Data", "Scripts", "Source", "User");
klaw(srcDirRoot).on("data", (item) => {
    if (item.stats.isFile() && path.extname(item.path) === ".psc") {
        const childPath = path.relative(srcDirRoot, item.path);

        fs.readFile(item.path, "utf8", (err, data) => {
            if (err) {
                return console.error(err);
            }

            fs.outputFile(path.join(debugOutDir, childPath), data, "utf8", (err) => {
                if (err) return console.log(err);
            });
            fs.outputFile(path.join(productOutDir, childPath), data.replace(/\s*.+;; Debug\r?\n/g, "\n"), "utf8", (err) => {
                if (err) return console.log(err);
            });
        });
    }
});
