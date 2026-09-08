# MAC-10: direct polling acceptance in System 7

Base: reviewed IO-01 integration `6b71813`. The Linux ordinary-source
normalpollprobe already covers pipe read/write readiness, invalid descriptors,
hangup, descriptor exhaustion and exact poll/pipe errno. It was not registered
or linked into the Mac application, so the guest suite did not exercise these
new calls directly.

The same unchanged source is now compiled privately for the Mac, registered
through a small libc descriptor, and added to the shared expected transcript.
The Linux suite already registers that command. No new runtime behavior or
claims about finite-timeout scheduling on the Mac are introduced. This expands
the startup suite to seventeen records; finite deadlines and lost-clock
injection remain separately tested on Linux. Exact CI and guest pending.
