# EDID: CEA-861 video data block iteration over-reads the raw EDID

`input.bin` is a complete 256-byte EDID. `xf86ForEachVideoBlock` -> `handle_cea_video_block` ->
`extract_cea_data_block` (`hw/xfree86/ddc/interpret_edid.c:383`) reads past the end of the buffer: a data
block length from the extension is not bounded by the extension's own size. ASan: heap-buffer-overflow.
Impact: out-of-bounds read from a monitor-supplied EDID. Fix: bound each data block by the end of the
128-byte extension before reading its payload.
