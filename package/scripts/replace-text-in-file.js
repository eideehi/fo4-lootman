const fs = require("fs-extra");

module.exports = {
    /**
     * Replace the text in the input file and write it to the output file.
     * @param {fs.PathLike} inputFile input file path
     * @param {string | RegExp} searchValue regex to replace
     * @param {string} replaceValue replacement string
     * @param {string} outputFile output file path
     */
    replace: (inputFile, regex, replacement, outputFile = inputFile) => {
        fs.readFile(inputFile, "utf8", (err, data) => {
            if (err) {
                return console.log(err);
            }
            const result = data.replace(regex, replacement);
            fs.outputFile(outputFile, result, "utf8", (err) => {
                if (err) return console.log(err);
            });
        });
    }
}
