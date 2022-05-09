const fs = require("fs-extra");
const path = require("path");
const { exec } = require("child_process");

const { archive2Path, buildTempDir } = require("./properties");

const filesRoot = path.join(buildTempDir, "files");

const tmpBa2Dir = path.join(filesRoot, "ba2", "tmp");
const debugBa2Dir = path.join(filesRoot, "ba2", "debug");
const productBa2Dir = path.join(filesRoot, "ba2", "product");

const papyrusRoot = path.join(filesRoot, "papyrus");
const resourcesRoot = path.join(filesRoot, "resources");

const debugBa2Path = path.join(debugBa2Dir, "LootMan - Main.ba2");
const productBa2Path = path.join(productBa2Dir, "LootMan - Main.ba2");

fs.removeSync(tmpBa2Dir);
fs.mkdirsSync(tmpBa2Dir);

fs.moveSync(path.join(papyrusRoot, "debug", "binary"), path.join(tmpBa2Dir, "Scripts"));
fs.copySync(path.join(resourcesRoot, "common", "Meshes"), path.join(tmpBa2Dir, "Meshes"));

fs.removeSync(debugBa2Dir);
fs.mkdirsSync(debugBa2Dir);

const debugArchiveCmd = `"${archive2Path}" "${tmpBa2Dir}" -r="${tmpBa2Dir}" -c="${debugBa2Path}"`;
exec(debugArchiveCmd, (error, stdout, stderr) => {
    if (error) {
        return console.error(error);
    }
    console.log(stdout);
    console.log(stderr);
}).on("close", () => {
    fs.removeSync(tmpBa2Dir);
    fs.mkdirsSync(tmpBa2Dir);

    fs.moveSync(path.join(papyrusRoot, "product", "binary"), path.join(tmpBa2Dir, "Scripts"));
    fs.removeSync(path.join(papyrusRoot, "product"));
    fs.moveSync(path.join(resourcesRoot, "common", "Meshes"), path.join(tmpBa2Dir, "Meshes"));

    fs.removeSync(productBa2Dir);
    fs.mkdirsSync(productBa2Dir);

    const productArchiveCmd = `"${archive2Path}" "${tmpBa2Dir}" -r="${tmpBa2Dir}" -c="${productBa2Path}"`;
    exec(productArchiveCmd, (error, stdout, stderr) => {
        if (error) {
            return console.error(error);
        }
        console.log(stdout);
        console.log(stderr);
    }).on("close", () => {
        fs.removeSync(tmpBa2Dir);
    });
});
