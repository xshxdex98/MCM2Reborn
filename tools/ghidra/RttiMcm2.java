// Stage 1.3 - Parse MSVC RTTI directly (Ghidra's analyzer recovered almost nothing here).
//
// MSVC 32-bit layout:
//   TypeDescriptor            +0 pVFTable  +4 spare  +8 name(".?AVBike@@")
//   CompleteObjectLocator     +0 signature +4 offset +8 cdOffset
//                             +12 pTypeDescriptor +16 pClassHierarchyDescriptor
//   ClassHierarchyDescriptor  +0 signature +4 attributes +8 numBaseClasses +12 pBaseClassArray
//   BaseClassDescriptor       +0 pTypeDescriptor +4 numContainedBases +8 PMD(mdisp,pdisp,vdisp)
//   vftable                   [vftable-4] = pCompleteObjectLocator
//
// Emits classes.json to mcm2_re\out\.
// @category OpenMCM2

import java.io.File;
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;

public class RttiMcm2 extends GhidraScript {

    // Output directory; the constant is only a default, the script argument wins.
    String OUT = "C:/mcm2ghidra/out";

    static String esc(String s) {
        if (s == null) return "null";
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    /** ".?AVFoo@Bar@@" -> "Bar::Foo" */
    static String demangle(String raw) {
        String s = raw;
        if (s.startsWith(".?AV") || s.startsWith(".?AU")) s = s.substring(4);
        while (s.endsWith("@")) s = s.substring(0, s.length() - 1);
        String[] parts = s.split("@");
        if (parts.length <= 1) return s;
        List<String> l = new ArrayList<>(Arrays.asList(parts));
        Collections.reverse(l);
        return String.join("::", l);
    }

    Memory mem;
    List<byte[]> blockBytes = new ArrayList<>();
    List<Address> blockStarts = new ArrayList<>();
    long textStart = Long.MAX_VALUE, textEnd = 0;

    long readU32(Address a) throws Exception {
        return mem.getInt(a) & 0xFFFFFFFFL;
    }

    /** All addresses in initialized memory holding a 4-byte LE value present in `targets`. */
    Map<Long, List<Long>> findPointersTo(Set<Long> targets) {
        Map<Long, List<Long>> hits = new HashMap<>();
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] buf = blockBytes.get(b);
            long base = blockStarts.get(b).getOffset();
            for (int i = 0; i + 4 <= buf.length; i++) {
                long v = (buf[i] & 0xffL) | ((buf[i + 1] & 0xffL) << 8)
                       | ((buf[i + 2] & 0xffL) << 16) | ((buf[i + 3] & 0xffL) << 24);
                if (targets.contains(v))
                    hits.computeIfAbsent(v, k -> new ArrayList<>()).add(base + i);
            }
        }
        return hits;
    }

    @Override
    public void run() throws Exception {
        String[] _a = getScriptArgs();
        if (_a.length > 0 && _a[0].length() > 0) OUT = _a[0];
        new File(OUT).mkdirs();
        mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();

        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            byte[] buf = new byte[(int) blk.getSize()];
            blk.getBytes(blk.getStart(), buf);
            blockBytes.add(buf);
            blockStarts.add(blk.getStart());
        }
        // Needed during vftable discovery, so compute it up front.
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute()) continue;
            textStart = Math.min(textStart, blk.getStart().getOffset());
            textEnd = Math.max(textEnd, blk.getEnd().getOffset());
        }
        println("STATS text_range=" + Long.toHexString(textStart) + "-" + Long.toHexString(textEnd));

        // ---- 1. TypeDescriptors, via their ".?AV"/".?AU" name strings ----
        Map<Long, String> typeDesc = new HashMap<>();   // TypeDescriptor addr -> class name
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] buf = blockBytes.get(b);
            long base = blockStarts.get(b).getOffset();
            for (int i = 0; i + 4 < buf.length; i++) {
                if (buf[i] != '.' || buf[i + 1] != '?' || buf[i + 2] != 'A') continue;
                char k = (char) buf[i + 3];
                if (k != 'V' && k != 'U') continue;
                int j = i;
                while (j < buf.length && buf[j] != 0) j++;
                String raw = new String(buf, i, j - i, "ASCII");
                if (!raw.endsWith("@@")) continue;
                typeDesc.put(base + i - 8, demangle(raw));   // name sits at TypeDescriptor+8
            }
        }
        println("STATS type_descriptors=" + typeDesc.size());

        // ---- 2. CompleteObjectLocators point at a TypeDescriptor from offset +12 ----
        Map<Long, String> cols = new HashMap<>();       // COL addr -> class name
        Map<Long, Long> colToTd = new HashMap<>();
        for (Map.Entry<Long, List<Long>> e : findPointersTo(typeDesc.keySet()).entrySet()) {
            for (long loc : e.getValue()) {
                long col = loc - 12;
                try {
                    if (readU32(toAddr(col)) != 0) continue;   // signature must be 0 (32-bit)
                } catch (Exception ex) { continue; }
                cols.put(col, typeDesc.get(e.getKey()));
                colToTd.put(col, e.getKey());
            }
        }
        println("STATS complete_object_locators=" + cols.size());

        // ---- 3. vftables sit 4 bytes after a pointer to their COL ----
        Map<String, List<Long>> classVftables = new TreeMap<>();
        Map<Long, Long> vftToCol = new HashMap<>();
        for (Map.Entry<Long, List<Long>> e : findPointersTo(cols.keySet()).entrySet()) {
            for (long loc : e.getValue()) {
                long vft = loc + 4;
                long slot0;
                try { slot0 = readU32(toAddr(vft)); }
                catch (Exception ex) { continue; }

                // Accept on "slot 0 points into code", NOT on "a function already
                // exists there". Virtual methods reachable only through a vtable are
                // precisely the ones auto-analysis never turned into functions, so
                // requiring one here silently discarded whole classes (Bike among them).
                if (slot0 < textStart || slot0 > textEnd) continue;
                if (fm.getFunctionAt(toAddr(slot0)) == null) {
                    try {
                        disassemble(toAddr(slot0));
                        createFunction(toAddr(slot0), null);
                    } catch (Exception ex) { /* keep the vftable regardless */ }
                }
                classVftables.computeIfAbsent(cols.get(e.getKey()), k -> new ArrayList<>()).add(vft);
                vftToCol.put(vft, e.getKey());
            }
        }
        println("STATS classes_with_vftable=" + classVftables.size());

        // ---- 4. Base classes, via the ClassHierarchyDescriptor ----
        Map<String, List<String>> bases = new TreeMap<>();
        for (Map.Entry<Long, String> e : cols.entrySet()) {
            try {
                long chd = readU32(toAddr(e.getKey() + 16));
                long n = readU32(toAddr(chd + 8));
                long arr = readU32(toAddr(chd + 12));
                if (n == 0 || n > 64) continue;
                List<String> bl = new ArrayList<>();
                for (int i = 0; i < n; i++) {
                    long bcd = readU32(toAddr(arr + i * 4L));
                    long td = readU32(toAddr(bcd));
                    String nm = typeDesc.get(td);
                    if (nm != null && !nm.equals(e.getValue())) bl.add(nm);
                }
                if (!bl.isEmpty()) bases.put(e.getValue(), bl);
            } catch (Exception ex) { /* malformed chain, skip */ }
        }
        println("STATS classes_with_bases=" + bases.size());

        // ---- 5. Walk vftable slots and emit ----
        // Virtual methods are often reachable ONLY through the vtable, so Ghidra's
        // auto-analysis never created functions for them. Create them here, and stop
        // the walk at the executable-section bounds or at the next vtable rather than
        // at the first address that happens to lack a function.
        Set<Long> vftStarts = new HashSet<>();
        for (List<Long> l : classVftables.values()) vftStarts.addAll(l);

        // Pass 1: walk every vtable, creating missing functions, WITHOUT naming anything yet.
        Map<String, List<long[]>> walked = new TreeMap<>();   // class -> list of {vftAddr, targets...}
        int totalSlots = 0, created = 0;
        for (Map.Entry<String, List<Long>> e : classVftables.entrySet()) {
            List<long[]> vts = new ArrayList<>();
            for (long vft : e.getValue()) {
                List<Long> targets = new ArrayList<>();
                for (int i = 0; i < 512; i++) {
                    long slotAddr = vft + i * 4L;
                    if (i > 0 && vftStarts.contains(slotAddr)) break;   // next vtable begins
                    long target;
                    try { target = readU32(toAddr(slotAddr)); } catch (Exception ex) { break; }
                    if (target < textStart || target > textEnd) break;  // out of code

                    Address ta = toAddr(target);
                    Function fn = fm.getFunctionAt(ta);
                    if (fn == null) {
                        try {
                            disassemble(ta);
                            fn = createFunction(ta, null);
                            if (fn != null) created++;
                        } catch (Exception ex) { /* fall through */ }
                    }
                    if (fn == null) break;
                    targets.add(target);
                    totalSlots++;
                }
                long[] row = new long[targets.size() + 1];
                row[0] = vft;
                for (int i = 0; i < targets.size(); i++) row[i + 1] = targets.get(i);
                vts.add(row);
            }
            walked.put(e.getKey(), vts);
        }
        println("STATS total_vslots=" + totalSlots);
        println("STATS functions_created=" + created);

        // Pass 2: decide the owner of each method. A method appearing at the same slot in
        // several vtables was inherited, not overridden, so it belongs to the most basal
        // class that has it (fewest base classes). Alphabetical tie-break for determinism.
        Map<Long, String> owner = new HashMap<>();
        Map<Long, Integer> ownerSlot = new HashMap<>();
        Map<Long, Integer> ownerDepth = new HashMap<>();
        for (Map.Entry<String, List<long[]>> e : walked.entrySet()) {
            String cls = e.getKey();
            List<String> bl = bases.get(cls);
            int depth = (bl == null) ? 0 : bl.size();
            for (long[] row : e.getValue()) {
                for (int i = 1; i < row.length; i++) {
                    long t = row[i];
                    Integer cur = ownerDepth.get(t);
                    if (cur == null || depth < cur || (depth == cur && cls.compareTo(owner.get(t)) < 0)) {
                        owner.put(t, cls);
                        ownerDepth.put(t, depth);
                        ownerSlot.put(t, i - 1);
                    }
                }
            }
        }
        int inherited = totalSlots - owner.size();
        println("STATS distinct_methods=" + owner.size());
        println("STATS inherited_slot_entries=" + inherited);

        // Pass 3: apply names (idempotent - only touches FUN_* and previously generated vf_*).
        int renamed = 0;
        for (Map.Entry<Long, String> e : owner.entrySet()) {
            Function fn = fm.getFunctionAt(toAddr(e.getKey()));
            if (fn == null) continue;
            String local = fn.getName();
            if (!(local.startsWith("FUN_") || local.matches("vf_\\d+"))) continue;
            try {
                Namespace ns = currentProgram.getSymbolTable().getOrCreateNameSpace(
                        currentProgram.getGlobalNamespace(), e.getValue(), SourceType.IMPORTED);
                fn.setParentNamespace(ns);
                fn.setName("vf_" + ownerSlot.get(e.getKey()), SourceType.IMPORTED);
                renamed++;
            } catch (Exception ex) { /* keep existing name */ }
        }
        println("STATS methods_named=" + renamed);

        // Pass 3b: recover class sizes from the `operator new` argument at construction
        // sites. Pattern:  push <size> / call operator new / ... / mov ecx,eax / call ctor
        // A constructor is identified by it writing a known vftable pointer into `this`.
        Map<Long, String> vftToClass = new HashMap<>();
        for (Map.Entry<String, List<Long>> e : classVftables.entrySet())
            for (long v : e.getValue()) vftToClass.put(v, e.getKey());

        Map<Long, String> ctorToClass = new HashMap<>();   // ctor entry -> class
        for (Map.Entry<Long, String> e : vftToClass.entrySet()) {
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(toAddr(e.getKey()))) {
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                if (fn == null) continue;
                ctorToClass.putIfAbsent(fn.getEntryPoint().getOffset(), e.getValue());
            }
        }
        println("STATS candidate_constructors=" + ctorToClass.size());

        // The game uses its own allocator (BlockAllocator.cpp), so keying off `operator new`
        // finds almost nothing. Work backwards from CONSTRUCTOR call sites instead, which is
        // allocator-agnostic. Heap construction in MSVC looks like:
        //     push <size> / call <allocator> / add esp,4 / test eax,eax / je .. / mov ecx,eax / call <ctor>
        // Requiring the `mov ecx,eax` keeps stack-allocated objects (no allocation) out.
        Listing listing = currentProgram.getListing();
        Map<String, Map<Long, Integer>> sizeVotes = new TreeMap<>();
        Map<Long, Integer> allocatorVotes = new HashMap<>();
        int ctorSites = 0, matchedSites = 0;

        for (Map.Entry<Long, String> ce : ctorToClass.entrySet()) {
            for (Reference r : currentProgram.getReferenceManager()
                    .getReferencesTo(toAddr(ce.getKey()))) {
                if (!r.getReferenceType().isCall()) continue;
                Instruction call = listing.getInstructionAt(r.getFromAddress());
                if (call == null) continue;
                ctorSites++;

                boolean sawThisFromEax = false;
                Long size = null, allocator = null;
                Instruction p = call;
                for (int k = 0; k < 16 && p != null; k++) {
                    p = p.getPrevious();
                    if (p == null) break;
                    String flat = p.toString().replace(" ", "").toUpperCase();

                    if (flat.equals("MOVECX,EAX")) { sawThisFromEax = true; continue; }

                    if (p.getMnemonicString().equalsIgnoreCase("CALL") && sawThisFromEax) {
                        Reference[] fr = p.getReferencesFrom();
                        for (Reference a : fr)
                            if (a.getReferenceType().isCall()) allocator = a.getToAddress().getOffset();
                        // the size is pushed immediately before the allocator call
                        Instruction q = p;
                        for (int j = 0; j < 4 && q != null; j++) {
                            q = q.getPrevious();
                            if (q == null) break;
                            if (!q.getMnemonicString().equalsIgnoreCase("PUSH")) continue;
                            Object[] ops = q.getOpObjects(0);
                            if (ops.length == 1 && ops[0] instanceof Scalar)
                                size = ((Scalar) ops[0]).getUnsignedValue();
                            break;
                        }
                        break;
                    }
                }
                if (size == null || size == 0 || size > 0x10000) continue;
                matchedSites++;
                sizeVotes.computeIfAbsent(ce.getValue(), x -> new HashMap<>()).merge(size, 1, Integer::sum);
                if (allocator != null) allocatorVotes.merge(allocator, 1, Integer::sum);
            }
        }
        println("STATS ctor_call_sites=" + ctorSites);
        println("STATS ctor_sites_with_alloc=" + matchedSites);
        for (Map.Entry<Long, Integer> a : allocatorVotes.entrySet()) {
            Function af = fm.getFunctionAt(toAddr(a.getKey()));
            println("ALLOCATOR " + toAddr(a.getKey()) + " "
                    + (af == null ? "?" : af.getName(true)) + " sites=" + a.getValue());
        }

        Map<String, Long> classSize = new TreeMap<>();
        Map<String, Integer> classSizeConf = new TreeMap<>();
        int ambiguous = 0;
        for (Map.Entry<String, Map<Long, Integer>> e : sizeVotes.entrySet()) {
            long best = -1; int bestN = 0, total = 0;
            for (Map.Entry<Long, Integer> v : e.getValue().entrySet()) {
                total += v.getValue();
                if (v.getValue() > bestN) { bestN = v.getValue(); best = v.getKey(); }
            }
            if (e.getValue().size() > 1) ambiguous++;
            classSize.put(e.getKey(), best);
            classSizeConf.put(e.getKey(), bestN == total ? bestN : -bestN);  // negative = disputed
        }
        println("STATS classes_with_size=" + classSize.size());
        println("STATS classes_size_disputed=" + ambiguous);

        // Pass 4: emit
        StringBuilder sb = new StringBuilder("{\n");
        boolean first = true;
        for (Map.Entry<String, List<long[]>> e : walked.entrySet()) {
            String cls = e.getKey();
            if (!first) sb.append(",\n");
            first = false;
            sb.append(" ").append(esc(cls)).append(": {");
            sb.append("\"size\":").append(classSize.containsKey(cls) ? classSize.get(cls) : "null")
              .append(",\"size_votes\":").append(classSizeConf.containsKey(cls) ? classSizeConf.get(cls) : 0)
              .append(",");
            List<String> bl = bases.get(cls);
            sb.append("\"bases\":[");
            if (bl != null) {
                List<String> q = new ArrayList<>();
                for (String x : bl) q.add(esc(x));
                sb.append(String.join(",", q));
            }
            sb.append("],\"vftables\":[");
            List<String> vts = new ArrayList<>();
            for (long[] row : e.getValue()) {
                List<String> slots = new ArrayList<>();
                for (int i = 1; i < row.length; i++) {
                    Function fn = fm.getFunctionAt(toAddr(row[i]));
                    String nm = (fn == null) ? "?" : fn.getName(true);
                    slots.add("{\"slot\":" + (i - 1) + ",\"addr\":\"" + toAddr(row[i])
                            + "\",\"name\":" + esc(nm)
                            + ",\"owner\":" + esc(owner.get(row[i]))
                            + ",\"introduced\":" + (cls.equals(owner.get(row[i]))) + "}");
                }
                vts.add("{\"addr\":\"" + toAddr(row[0]) + "\",\"slot_count\":" + slots.size()
                        + ",\"slots\":[" + String.join(",", slots) + "]}");
            }
            sb.append(String.join(",", vts)).append("]}");
        }
        sb.append("\n}\n");

        try (PrintWriter w = new PrintWriter(new File(OUT, "classes.json"), "UTF-8")) { w.print(sb); }
        println("WROTE " + new File(OUT, "classes.json"));

        List<Map.Entry<String, List<Long>>> top = new ArrayList<>(classVftables.entrySet());
        top.sort((a, b) -> b.getValue().size() - a.getValue().size());
        for (int i = 0; i < Math.min(15, top.size()); i++)
            println("CLASS " + top.get(i).getKey() + " vftables=" + top.get(i).getValue().size());
    }
}
