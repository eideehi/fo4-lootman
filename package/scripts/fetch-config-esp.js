const fs = require("fs-extra");
const path = require("path");
const klaw = require("klaw");

const { resourcesRoot, fallout4Dir } = require("./properties");

const projectLootmanFilesDir = path.join(resourcesRoot, "lootman", "en");

fs.removeSync(projectLootmanFilesDir);

const dataDir = path.resolve(fallout4Dir, "Data");

/**
 * Copy the files under the Data directory in Fallout 4 to the specified directory.
 * @param {string} src source path
 * @param {string} to destination path
 */
const copy = (src, to) => {
    const relativePath = path.relative(dataDir, src);
    const targetPath = path.join(to, relativePath);
    fs.copySync(src, targetPath);
}

const injectionDataDir = path.resolve(dataDir, "Lootman");
klaw(injectionDataDir).on("data", (item) => {
    if (item.stats.isFile() && path.extname(item.path) === ".json") {
        copy(item.path, projectLootmanFilesDir);
    }
});

const mcmConfigDir = path.resolve(dataDir, "MCM", "Config", "Lootman");
klaw(mcmConfigDir).on("data", (item) => {
    if (item.stats.isFile() && path.extname(item.path) === ".json") {
        copy(item.path, projectLootmanFilesDir);
    }
});

copy(path.resolve(dataDir, "Lootman.esp"), projectLootmanFilesDir);
