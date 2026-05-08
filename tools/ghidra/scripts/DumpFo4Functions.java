// Dumps decompiler output, instructions, and direct references for selected
// Fallout 4 functions. Intended for headless use after importing Fallout4.exe.

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DumpFo4Functions extends GhidraScript {
    private static final int MAX_INSTRUCTIONS = 500;
    private static final int MAX_REFERENCES = 200;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: DumpFo4Functions <output-file> <address> [address...]");
            return;
        }

        File outFile = new File(args[0]);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        List<Address> targets = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            targets.add(parseTargetAddress(args[i]));
        }

        try (PrintWriter out = new PrintWriter(outFile)) {
            out.printf("Program: %s%n", currentProgram.getName());
            out.printf("Image base: %s%n%n", currentProgram.getImageBase());

            DecompInterface decompiler = new DecompInterface();
            try {
                decompiler.openProgram(currentProgram);
                for (Address target : targets) {
                    dumpTarget(out, decompiler, target);
                }
            } finally {
                decompiler.dispose();
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

    private void dumpTarget(PrintWriter out, DecompInterface decompiler, Address target) {
        out.printf("================================================================================%n");
        out.printf("Target %s%n", target);

        Function function = getFunctionAt(target);
        if (function == null) {
            function = getFunctionContaining(target);
        }

        if (function == null) {
            out.println("No function found.");
            out.println();
            dumpReferencesTo(out, target);
            return;
        }

        out.printf("Function: %s%n", function.getName(true));
        out.printf("Entry: %s%n", function.getEntryPoint());
        out.printf("Body: %s%n", function.getBody());
        out.println();

        dumpReferencesTo(out, function.getEntryPoint());
        dumpOutgoingReferences(out, function);
        dumpInstructions(out, function);
        dumpDecompiler(out, decompiler, function);
        out.println();
    }

    private void dumpReferencesTo(PrintWriter out, Address address) {
        ReferenceManager references = currentProgram.getReferenceManager();
        ReferenceIterator iterator = references.getReferencesTo(address);
        out.println("References to entry:");
        int count = 0;
        while (iterator.hasNext() && count < MAX_REFERENCES) {
            Reference ref = iterator.next();
            out.printf("  %s -> %s type=%s%n", ref.getFromAddress(), ref.getToAddress(), ref.getReferenceType());
            count++;
        }
        if (count == 0) {
            out.println("  <none>");
        }
        if (iterator.hasNext()) {
            out.println("  ... truncated ...");
        }
        out.println();
    }

    private void dumpOutgoingReferences(PrintWriter out, Function function) {
        ReferenceManager references = currentProgram.getReferenceManager();
        out.println("Outgoing references from function body:");
        int count = 0;
        for (Address address : function.getBody().getAddresses(true)) {
            Reference[] refs = references.getReferencesFrom(address);
            for (Reference ref : refs) {
                if (!ref.getReferenceType().isFlow()) {
                    continue;
                }
                out.printf("  %s -> %s type=%s%n", ref.getFromAddress(), ref.getToAddress(), ref.getReferenceType());
                count++;
                if (count >= MAX_REFERENCES) {
                    out.println("  ... truncated ...");
                    out.println();
                    return;
                }
            }
        }
        if (count == 0) {
            out.println("  <none>");
        }
        out.println();
    }

    private void dumpInstructions(PrintWriter out, Function function) {
        out.println("Instructions:");
        int count = 0;
        for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
            out.printf("  %s: %s%n", instruction.getAddress(), instruction);
            count++;
            if (count >= MAX_INSTRUCTIONS) {
                out.println("  ... truncated ...");
                break;
            }
        }
        out.println();
    }

    private void dumpDecompiler(PrintWriter out, DecompInterface decompiler, Function function) {
        out.println("Decompiled C:");
        DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
        if (results != null && results.decompileCompleted() && results.getDecompiledFunction() != null) {
            out.println(results.getDecompiledFunction().getC());
        } else {
            out.printf("  <decompile failed: %s>%n",
                results == null ? "no results" : results.getErrorMessage());
        }
    }
}
