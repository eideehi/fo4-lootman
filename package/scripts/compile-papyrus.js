const fs = require("fs-extra");
const path = require("path");
const klaw = require("klaw");
const xxHash = require("xxhashjs");
const { exec } = require("child_process");

const { papyrusCompilerPath, papyrusImportDirs, buildDirRoot, buildTempDir } = require("./properties");
const { replace } = require("./replace-text-in-file");

const papyrusCacheDir = path.join(buildDirRoot, "cache", "papyrus");
const debugCacheDir = path.join(papyrusCacheDir, "binary", "debug");
const productCacheDir = path.join(papyrusCacheDir, "binary", "product");
const scriptHashesPath = path.join(papyrusCacheDir, "script-hashes.json");
if (!fs.existsSync(scriptHashesPath)) {
    fs.mkdirsSync(path.dirname(scriptHashesPath));
    fs.writeJSONSync(scriptHashesPath, {});
}
let scriptHashes = fs.readJSONSync(scriptHashesPath);

const papyrusDirRoot = path.join(buildTempDir, "files", "papyrus");
const debugDir = path.join(papyrusDirRoot, "debug");
const sourceDir = path.join(debugDir, "source");
const debugOutDir = path.join(debugDir, "binary");
const productOutDir = path.join(papyrusDirRoot, "product", "binary");

const papyrusProjectTemplate = path.join(__dirname, "papyrus.ppj");
const debugPapyrusProject = path.join(sourceDir, "debug-papyrus.ppj");
const productPapyrusProject = path.join(sourceDir, "product-papyrus.ppj");

let imports = "<Import>.</Import>";
papyrusImportDirs.forEach((dir) => {
    imports += `\n<Import>${dir}</Import>`;
});

let newHashes = {};

const deployDebug = () => {
    fs.removeSync(debugOutDir);
    fs.mkdirsSync(debugOutDir);
    klaw(debugCacheDir).on("data", (item) => {
        if (item.stats.isFile() && path.extname(item.path) === ".pex") {
            const relativePath = path.relative(debugCacheDir, item.path);
            const parts = path.parse(relativePath);
            const scriptName = `${parts.dir.split(path.sep).join(":")}:${parts.name}`.toLowerCase();

            if (newHashes.hasOwnProperty(scriptName)) {
                fs.copy(item.path, path.join(debugOutDir, relativePath));
            }
        }
    });
};
const deployProduct = () => {
    fs.removeSync(productOutDir);
    fs.mkdirsSync(productOutDir);
    klaw(productCacheDir).on("data", (item) => {
        if (item.stats.isFile() && path.extname(item.path) === ".pex") {
            const relativePath = path.relative(productCacheDir, item.path);
            const parts = path.parse(relativePath);
            const scriptName = `${parts.dir.split(path.sep).join(":")}:${parts.name}`.toLowerCase();

            if (newHashes.hasOwnProperty(scriptName)) {
                fs.copy(item.path, path.join(productOutDir, relativePath));
            }
        }
    });
};

let scripts = "";
klaw(sourceDir).on("data", (item) => {
    if (item.stats.isFile() && path.extname(item.path) === ".psc") {
        const relativePath = path.relative(sourceDir, item.path);
        const parts = path.parse(relativePath);
        const scriptName = `${parts.dir.split(path.sep).join(":")}:${parts.name}`.toLowerCase();
        const hash = xxHash.h32(fs.readFileSync(item.path), 0x123).toString(16);

        newHashes[scriptName] = hash;

        if (!scriptHashes.hasOwnProperty(scriptName) || scriptHashes[scriptName] !== hash) {
            scripts += `\n<Script>${scriptName}</Script>`;
        }
    }
}).on("end", async () => {
    fs.writeJSONSync(scriptHashesPath, newHashes);

    if (scripts.length > 0) {
        replace(papyrusProjectTemplate, [
            {
                key: "__OUTPUT_DIR__",
                value: debugCacheDir
            },
            {
                key: /__IS_PRODUCT__/g,
                value: "false"
            },
            {
                key: "__IMPORTS__",
                value: imports
            },
            {
                key: "__SCRIPTS__",
                value: scripts
            }
        ], debugPapyrusProject);

        exec(`"${papyrusCompilerPath}" "${debugPapyrusProject}"`,
            {
                cwd: sourceDir,
            },
            (error, stdout, stderr) => {
                if (error) {
                    return console.error(error);
                }
                console.log(stdout);
                console.log(stderr);
            })
            .on("close", () => {
                console.log("debug pex compile end");
                fs.remove(debugPapyrusProject);
                deployDebug();
            });

        replace(papyrusProjectTemplate, [
            {
                key: "__OUTPUT_DIR__",
                value: productCacheDir
            },
            {
                key: /__IS_PRODUCT__/g,
                value: "true"
            },
            {
                key: "__IMPORTS__",
                value: imports
            },
            {
                key: "__SCRIPTS__",
                value: scripts
            }
        ], productPapyrusProject);

        exec(`"${papyrusCompilerPath}" "${productPapyrusProject}"`,
            {
                cwd: sourceDir,
            },
            (error, stdout, stderr) => {
                if (error) {
                    return console.error(error);
                }
                console.log(stdout);
                console.log(stderr);
            })
            .on("close", () => {
                console.log("product pex compile end");
                fs.remove(productPapyrusProject);
                deployProduct();
            });
    }
    else {
        deployDebug();
        deployProduct();
    }
});
