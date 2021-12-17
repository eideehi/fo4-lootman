require("dotenv").config();

if (process.env.FETCH_TARGET === undefined) {
    console.error("FETCH_TARGET is not defined");
    process.exit(1);
}

const fs = require("fs-extra");
const path = require("path");
const klaw = require("klaw");

const { papyrusSourceDir, projectRoot } = require("./properties");

const userSourceDir = path.resolve(papyrusSourceDir, "User");
const projectPapyrusDir = path.join(projectRoot, "papyrus", "Data");
const projectUserSourceDir = path.join(projectPapyrusDir, "Scripts", "Source", "User");

fs.removeSync(projectPapyrusDir);

const targets = process.env.FETCH_TARGET.split(";");
targets.forEach((target) => {
    const targetPath = path.resolve(userSourceDir, target);
    const stats = fs.statSync(targetPath);
    if (stats.isDirectory()) {
        klaw(targetPath).on("data", (item) => {
            if (item.stats.isFile() && path.extname(item.path) === ".psc") {
                const relativePath = path.relative(userSourceDir, item.path);
                fs.copySync(item.path, path.join(projectUserSourceDir, relativePath));
            }
        });
    } else if (stats.isFile()) {
        fs.copySync(targetPath, path.resolve(projectUserSourceDir, target));
    }
});
