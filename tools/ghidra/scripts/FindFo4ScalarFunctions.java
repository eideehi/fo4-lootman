// Finds functions whose instructions reference all requested scalar values.
// @category Fallout4

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class FindFo4ScalarFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: FindFo4ScalarFunctions <output-file> <scalar> [scalar...]");
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

        List<Long> wanted = new ArrayList<>();
        for (int i = 1; i < args.length; ++i) {
            wanted.add(Long.decode(args[i]));
        }

        try (PrintWriter out = new PrintWriter(outFile)) {
            out.printf("Program: %s%n", currentProgram.getName());
            out.printf("Image base: %s%n", currentProgram.getImageBase());
            out.printf("Scalars: %s%n%n", wanted);

            for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                Map<Long, Set<Address>> hits = new LinkedHashMap<>();
                for (Long value : wanted) {
                    hits.put(value, new LinkedHashSet<>());
                }

                for (Instruction ins : currentProgram.getListing().getInstructions(fn.getBody(), true)) {
                    collectScalarHits(ins, hits);
                }

                boolean matched = true;
                for (Long value : wanted) {
                    if (hits.get(value).isEmpty()) {
                        matched = false;
                        break;
                    }
                }
                if (!matched) {
                    continue;
                }

                out.printf("Function %s entry=%s body=%s%n", fn.getName(), fn.getEntryPoint(), fn.getBody());
                for (Long value : wanted) {
                    out.printf("  scalar 0x%X:%n", value);
                    int count = 0;
                    for (Address address : hits.get(value)) {
                        Instruction ins = currentProgram.getListing().getInstructionAt(address);
                        out.printf("    %s: %s%n", address, ins);
                        if (++count >= 12) {
                            out.println("    ...");
                            break;
                        }
                    }
                }
                out.println();
            }
        }

        println("Wrote " + outFile.getAbsolutePath());
    }

    private void collectScalarHits(Instruction ins, Map<Long, Set<Address>> hits) {
        for (int opIndex = 0; opIndex < ins.getNumOperands(); ++opIndex) {
            for (Object obj : ins.getOpObjects(opIndex)) {
                if (obj instanceof Scalar scalar) {
                    long value = scalar.getUnsignedValue();
                    Set<Address> addresses = hits.get(value);
                    if (addresses != null) {
                        addresses.add(ins.getAddress());
                    }
                }
            }
        }
    }
}
