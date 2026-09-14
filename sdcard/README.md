# SD card contents

Copy the contents of this directory to the root of a FAT32-formatted microSD
card before inserting it into the M5Stack Basic.

The AquesTalk dictionary must be located at:

```text
/aq_dic/aqdic_m.bin
```

The dictionary binary is supplied by AQUEST and is intentionally excluded from
Git because redistribution is prohibited.

The Japanese UI Smooth Font must be located at:

```text
/Japanese16.vlw
```

This font is generated from Noto Sans CJK JP and is tracked in this directory.
If it is missing, the firmware keeps running and uses the English UI fallback.
