// Export a compact inventory from a Ghidra program.
//
// Run through ghidra_export.sh, which sets NEXUS_GHIDRA_OUT.  The output is a
// tab-separated inventory of functions, symbols and external references; it is
// intended for cross-checking the ELF dynsym-derived facts in the reports.
//
// @category NEXUS

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.File;
import java.io.PrintWriter;

public class ExportNexusInventory extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = System.getenv("NEXUS_GHIDRA_OUT");
        if (outDir == null || outDir.isEmpty()) {
            outDir = System.getProperty("java.io.tmpdir");
        }
        File dir = new File(outDir);
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IllegalStateException("cannot create " + dir);
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        try (PrintWriter fw = new PrintWriter(new File(dir, "functions.tsv"))) {
            fw.println("address\tsize\tthunk\tname");
            for (Function f : fm.getFunctions(true)) {
                fw.printf("%s\t%d\t%b\t%s%n",
                        f.getEntryPoint(),
                        f.getBody().getNumAddresses(),
                        f.isThunk(),
                        f.getName(true));
            }
        }

        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter sw = new PrintWriter(new File(dir, "symbols.tsv"))) {
            sw.println("address\ttype\texternal\tname");
            SymbolIterator it = st.getAllSymbols(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Symbol s = it.next();
                Address a = s.getAddress();
                sw.printf("%s\t%s\t%b\t%s%n",
                        a == null ? "?" : a.toString(),
                        s.getSymbolType(),
                        s.isExternal(),
                        s.getName(true));
            }
        }

        try (PrintWriter iw = new PrintWriter(new File(dir, "imports.tsv"))) {
            iw.println("name\tlibrary");
            for (Symbol s : st.getExternalSymbols()) {
                iw.printf("%s\t%s%n", s.getName(true),
                        s.getParentNamespace() == null ? "" : s.getParentNamespace().getName(true));
            }
        }

        println("NEXUS inventory written to " + dir.getAbsolutePath());
    }
}
