const fs = require("fs-extra");
const path = require("path");
const klaw = require("klaw");

const { projectRoot, buildTempDir } = require("./properties");

const outDir = path.join(buildTempDir, "files", "debug", "papyrus-source");
const srcDirRoot = path.join(projectRoot, "papyrus", "Data", "Scripts", "Source", "User");

fs.removeSync(outDir);

klaw(srcDirRoot).on("data", (item) => {
    if (item.stats.isFile() && path.extname(item.path) === ".psc") {
        const relativePath = path.relative(srcDirRoot, item.path);
        const outPath = path.join(outDir, relativePath);
        fs.copy(item.path, outPath)
          .then(() => {
              console.log(`copy ${item.path} -> ${outPath}`);
          })
          .catch((err) => {
              console.error(err);
          });
    }
});
