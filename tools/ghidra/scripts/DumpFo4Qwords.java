// Dumps qwords from selected Fallout 4 addresses.
// @category Fallout4

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class DumpFo4Qwords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("Usage: DumpFo4Qwords <output-file> <qword-count> <address> [address...]");
            return;
        }

        File outFile = new File(args[0]);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        int qwordCount = Integer.parseInt(args[1]);
        try (PrintWriter out = new PrintWriter(outFile)) {
            out.printf("Program: %s%n", currentProgram.getName());
            out.printf("Image base: %s%n%n", currentProgram.getImageBase());

            for (int i = 2; i < args.length; i++) {
                Address address = parseTargetAddress(args[i]);
                dumpQwords(out, address, qwordCount);
            }
        }

        println("Wrote " + outFile.getAbsolutePath());
    }

    private Address parseTargetAddress(String value) {
        String text = value.trim();
        if (text.startsWith("0x") || text.startsWith("0X")) {
            text = text.substring(2);
        }
        return toAddr(Long.parseUnsignedLong(text, 16));
    }

    private void dumpQwords(PrintWriter out, Address address, int qwordCount) throws Exception {
        out.printf("================================================================================%n");
        out.printf("Target %s%n", address);

        Address current = address;
        for (int i = 0; i < qwordCount; i++) {
            long value = getLong(current);
            Address target = toAddr(value);
            Function function = getFunctionAt(target);
            if (function == null) {
                function = getFunctionContaining(target);
            }
            out.printf("  %s: %016X", current, value);
            if (function != null) {
                out.printf(" -> %s entry=%s", function.getName(true), function.getEntryPoint());
            }
            out.println();
            current = current.add(8);
        }
        out.println();
    }
}
