# MAC-13: retired boot-copy disk budget

Documentation only. Repeated calibrated guest runs each retain a roughly 1 GiB
boot copy. During the team's 2026-09-08 work, an agent application displayed an
ENOSPC write error. The coordinator inspected the alert and free space, checked
accepted receipts and fresh lsof results, and removed only older closed copied
System.dsk files. Source trees and all other acceptance artifacts were preserved.
Free space rose to about 8 GiB before the next staged guest. The application
resumed after the inspected alert was dismissed; the subsequent 57-record guest
run completed normally. This is observed maintenance evidence, not a test of
an automatic cleanup feature.

The automation README now specifies the copied-file/receipt/slot/open-file
checks and preservation boundary. It adds no code, deletion command, or runtime
acceptance rule. Exact documentation CI is pending; no new guest run is required.
