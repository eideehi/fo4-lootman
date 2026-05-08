// Finds defined strings containing the requested text and dumps references.
// @category Fallout4

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class FindFo4StringRefs extends GhidraScript {
    private static final int MAX_REFS_PER_STRING = 80;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: FindFo4StringRefs <output-file> <substring> [substring...]");
            return;
        }

        File outFile = new File(args[0]);
        if (!outFile.isAbsolute()) {
            outFile = new File(System.getProperty("user.dir"), args[0]);
        }
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        List<String> needles = new ArrayList<>();
        for (int i = 1; i < args.length; ++i) {
            needles.add(args[i].toLowerCase());
        }

        try (PrintWriter out = new PrintWriter(outFile)) {
            out.printf("Program: %s%n", currentProgram.getName());
            out.printf("Image base: %s%n", currentProgram.getImageBase());
            out.printf("Substrings: %s%n%n", needles);

            DataIterator iterator = currentProgram.getListing().getDefinedData(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Data data = iterator.next();
                Object value = data.getValue();
                if (!(value instanceof String)) {
                    continue;
                }
                String text = (String)value;
                String lower = text.toLowerCase();

                boolean matched = false;
                for (String needle : needles) {
                    if (lower.contains(needle)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    continue;
                }

                Address address = data.getMinAddress();
                out.printf("String %s len=%d value=\"%s\"%n", address, data.getLength(), escape(text));

                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
                int count = 0;
                while (refs.hasNext() && count < MAX_REFS_PER_STRING) {
                    Reference ref = refs.next();
                    Address from = ref.getFromAddress();
                    Function fn = getFunctionContaining(from);
                    out.printf("  ref %s -> %s type=%s function=%s entry=%s%n",
                        from,
                        ref.getToAddress(),
                        ref.getReferenceType(),
                        fn != null ? fn.getName(true) : "<none>",
                        fn != null ? fn.getEntryPoint().toString() : "<none>");
                    ++count;
                }
                if (count == 0) {
                    out.println("  ref <none>");
                } else if (refs.hasNext()) {
                    out.println("  ref ... truncated ...");
                }
                out.println();
            }
        }

        println("Wrote " + outFile.getAbsolutePath());
    }

    private String escape(String value) {
        return value
            .replace("\\", "\\\\")
            .replace("\n", "\\n")
            .replace("\r", "\\r")
            .replace("\t", "\\t")
            .replace("\"", "\\\"");
    }
}
