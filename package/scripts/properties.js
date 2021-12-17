require("dotenv").config();

if (process.env.STEAM_GAME_DIR === undefined) {
    console.error("STEAM_GAME_DIR is not defined");
    process.exit(1);
}

if (process.env.PROJECT_ROOT === undefined) {
    console.error("PROJECT_ROOT is not defined");
    process.exit(1);
}

const fs = require("fs-extra");
const path = require("path");

/**
 * Verify that the directory of the argument path string exists.
 * @param {string} dir Path of the directory to verify
 * @returns {string} Path of the directory where the verification was completed
 */
function checkDir(dir) {
    if (!fs.statSync(dir).isDirectory()) {
        console.error(`${fallout4Dir} is not a directory`);
        process.exit(1);
    }
    return dir;
}

/**
 * Verify that the file of the argument path string exists.
 * @param {string} file Path of the file to verify
 * @returns {string} Path of the file where the verification was completed
 */
function checkFile(file) {
    if (!fs.statSync(file).isFile()) {
        console.error(`${file} is not a file`);
        process.exit(1);
    }
    return file;
}

const fallout4Dir = checkDir(path.resolve(process.env.STEAM_GAME_DIR, "Fallout 4"));
const archive2Path = checkFile(path.resolve(fallout4Dir, "Tools", "Archive2", "Archive2.exe"));
const papyrusCompilerPath = checkFile(path.resolve(fallout4Dir, "Papyrus Compiler", "PapyrusCompiler.exe"));
const papyrusSourceDir = checkDir(path.resolve(fallout4Dir, "Data", "Scripts", "Source"));
const papyrusImportDirs = ["User", /*"DLC06", "DLC05", "DLC04", "DLC03", "DLC02", "DLC01",*/ "Base"].map((dirName) => path.resolve(papyrusSourceDir, dirName));

const projectRoot = checkDir(path.resolve(process.env.PROJECT_ROOT));
const resourcesRoot = checkDir(path.join(process.cwd(), "resources"));
const buildTempRoot = path.join(process.cwd(), ".build-temp");
const archiveName = `Lootman - ${process.env.npm_package_version}`;
const buildTempDir = path.join(buildTempRoot, archiveName);
fs.mkdirsSync(buildTempDir);

module.exports = {
    fallout4Dir,
    archive2Path,
    papyrusCompilerPath,
    papyrusSourceDir,
    papyrusImportDirs,
    projectRoot,
    resourcesRoot,
    buildTempRoot,
    archiveName,
    buildTempDir,
}
