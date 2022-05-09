const fs = require("fs-extra");

module.exports = {
    /**
     * @typedef Replacement
     * @type {object}
     * @property {string | RegExp} key regex to replace
     * @property {string} value replacement string
     */
    /**
     * Replace the text in the input file and write it to the output file.
     * @param {fs.PathLike} inputFile input file path
     * @param {Replacement[]} replacements array of elements to replace
     * @param {string} outputFile output file path
     */
    replace: (inputFile, replacements, outputFile = inputFile) => {
        let data = fs.readFileSync(inputFile, "utf8");
        replacements.forEach((replacement) => {
            data = data.replace(replacement.key, replacement.value);
        });
        fs.outputFileSync(outputFile, data);
    }
}
