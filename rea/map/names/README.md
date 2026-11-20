# names/ : function names from the RE coverage pass (2026-11-02, platform slice)
One TSV per image: addr (guest address; sndmfc.dll is rebased 0x10300000, Hopper/file base is 0x10000000: subtract 0x300000),
name, level (understood = body read; named = name by class/shape), confidence (strong = 2 sources or direct read; weak = shape only),
area, hopper_name (name made unique where two functions share one), summary, evidence.
Hopper does not keep edits after close_binary (checked 2026-11-02: a renamed address came back as sub_*), and Ghidra is not set up on this Mac
(no Java). These TSVs are the durable record; apply to a live Hopper session with REA set_addresses_names when needed.
