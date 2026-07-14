// Dumps instructions around selected Fallout 4 addresses.
// @category Fallout4

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryAccessException;

import java.io.File;
import java.io.PrintWriter;

public class DumpFo4InstructionWindow extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("Usage: DumpFo4InstructionWindow <output-file> <instruction-count> <address> [address...]");
            return;
        }

        File outFile = new File(args[0]);
        File parent = outFile.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        int instructionCount = Integer.parseInt(args[1]);
        try (PrintWriter out = new PrintWriter(outFile)) {
            out.printf("Program: %s%n", currentProgram.getName());
            out.printf("Image base: %s%n%n", currentProgram.getImageBase());

            for (int i = 2; i < args.length; i++) {
                Address address = parseTargetAddress(args[i]);
                dumpWindow(out, address, instructionCount, i + 1 < args.length);
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

    private void dumpWindow(PrintWriter out, Address address, int instructionCount, boolean addTrailingBlankLine) {
        out.printf("================================================================================%n");
        out.printf("Target %s%n", address);

        Instruction instruction = currentProgram.getListing().getInstructionAt(address);
        if (instruction == null) {
            instruction = currentProgram.getListing().getInstructionBefore(address);
        }
        if (instruction == null) {
            out.println("No instruction found.");
            if (addTrailingBlankLine) {
                out.println();
            }
            return;
        }

        for (int i = 0; i < instructionCount && instruction != null; i++) {
            out.printf("  %s: [%s] %s%n", instruction.getAddress(), formatBytes(instruction), instruction);
            instruction = instruction.getNext();
        }
        if (addTrailingBlankLine) {
            out.println();
        }
    }

    private String formatBytes(Instruction instruction) {
        byte[] bytes;
        try {
            bytes = instruction.getBytes();
        } catch (MemoryAccessException e) {
            throw new IllegalStateException("Unable to read instruction bytes at " + instruction.getAddress(), e);
        }

        StringBuilder result = new StringBuilder();
        for (int i = 0; i < bytes.length; i++) {
            if (i > 0) {
                result.append(' ');
            }
            result.append(String.format("%02X", bytes[i] & 0xFF));
        }
        return result.toString();
    }
}
