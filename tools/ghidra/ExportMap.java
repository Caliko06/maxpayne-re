// Ghidra headless post-script: one row per function (rea/map/<image>.tsv) and its decompiled C (rea/c/<image>/<addr>.c).
// The map is the input of the whole-game classification by subsystem (OWNER 2026-11-01: RE of the whole game first).
// Usage: analyzeHeadless <proj dir> <name> -import <img> -postScript ExportMap.java <repo root> [nodecomp]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.StringDataInstance;
import java.io.*;
import java.util.*;

public class ExportMap extends GhidraScript {
    static String clean(String s) { return s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' '); }

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        String root = a[0];
        boolean decomp = !(a.length > 1 && a[1].equals("nodecomp"));
        String img = currentProgram.getName();
        new File(root + "/rea/map").mkdirs();
        File cdir = new File(root + "/rea/c/" + img);
        cdir.mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(root + "/rea/map/" + img + ".tsv"));
        out.println("entry\tsize\tname\tnamespace\tcallers\tcallees\timports\tstrings\tcaller_names");
        DecompInterface di = null;
        if (decomp) { di = new DecompInterface(); di.openProgram(currentProgram); }
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing L = currentProgram.getListing();
        int n = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            if (f.isExternal()) continue;
            Set<Function> callees = f.getCalledFunctions(monitor), callers = f.getCallingFunctions(monitor);
            List<String> ce = new ArrayList<>(), im = new ArrayList<>(), cr = new ArrayList<>(), st = new ArrayList<>();
            for (Function c : callees) {
                Function t = c.isThunk() ? c.getThunkedFunction(true) : c;
                if (t != null && t.isExternal()) im.add(t.getExternalLocation().getLibraryName() + "!" + t.getName());
                else ce.add(c.getName(true));
            }
            for (Function c : callers) if (cr.size() < 8) cr.add(c.getName(true));
            for (Address ad : f.getBody().getAddresses(true)) {
                for (Reference r : rm.getReferencesFrom(ad)) {
                    Data d = L.getDataAt(r.getToAddress());
                    if (d != null && d.hasStringValue() && st.size() < 8) {
                        String s = clean(String.valueOf(d.getValue()));
                        st.add(s.length() > 80 ? s.substring(0, 80) : s);
                    }
                }
            }
            String ns = f.getParentNamespace() == null ? "" : f.getParentNamespace().getName(true);
            out.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + clean(f.getName()) + "\t" + clean(ns) + "\t"
                + callers.size() + "\t" + clean(String.join(",", ce.subList(0, Math.min(16, ce.size())))) + "\t" + clean(String.join(",", im))
                + "\t" + String.join(" | ", st) + "\t" + clean(String.join(",", cr)));
            if (di != null) {
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted()) {
                    try (PrintWriter w = new PrintWriter(new FileWriter(new File(cdir, f.getEntryPoint() + ".c")))) {
                        w.println("// " + img + " " + f.getEntryPoint() + " " + f.getName(true));
                        w.print(r.getDecompiledFunction().getC());
                    }
                }
            }
            if (++n % 500 == 0) println("ExportMap " + img + ": " + n);
        }
        out.close();
        println("ExportMap " + img + " done: " + n + " functions");
    }
}
