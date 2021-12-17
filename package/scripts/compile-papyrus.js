const fs = require("fs-extra");
const path = require("path");
const { exec } = require("child_process");

const { papyrusCompilerPath, papyrusImportDirs, buildTempDir } = require("./properties");

const importDirs = `${papyrusImportDirs.join(";")}`;

const commonOutDir = path.join(buildTempDir, "files");

const debugDirRoot = path.join(commonOutDir, "debug");
const debugSourceDir = path.join(debugDirRoot, "papyrus-source");
const debugOutDir = path.join(debugDirRoot, "papyrus-binary");
const debugCompileCmd = `"${papyrusCompilerPath}" "${debugSourceDir}" -i="${debugSourceDir};${importDirs}" -o="${debugOutDir}" -f="Institute_Papyrus_Flags.flg" -op -a`;

fs.removeSync(debugOutDir);
fs.mkdirsSync(debugOutDir);
exec(debugCompileCmd,
    (error, stdout, stderr) => {
        if (error) {
            return console.error(error);
        }
        console.log(stdout);
        console.log(stderr);
    })
    .on("close", () => {
        console.log("debug pex compile end");
    });


const productDir = path.join(commonOutDir, "product");
const productSourceDir = path.join(productDir, ".papyrus-source");
const productOutDir = path.join(productDir, ".archive-main", "Scripts");
const productCompileCmd = `"${papyrusCompilerPath}" "${productSourceDir}" -i="${productSourceDir};${importDirs}" -o="${productOutDir}" -f="Institute_Papyrus_Flags.flg" -op -r -final -a`;

fs.removeSync(productOutDir);
fs.mkdirsSync(productOutDir);
exec(productCompileCmd,
    (error, stdout, stderr) => {
        if (error) {
            return console.error(error);
        }
        console.log(stdout);
        console.log(stderr);
    })
    .on("close", () => {
        console.log("product pex compile end");
    });
