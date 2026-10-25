# Weather fax line synchronisation

With **auto slant** enabled, the weather fax decoder keeps checking the line phase while it receives a picture, whether it locked to the initial
phasing signal or joined a picture already under way. A missing radio/USB transfer removes a piece of a line: it causes an abrupt horizontal
displacement, not a change in the station's clock rate. Correcting the whole picture's slant cannot repair that step.

The decoder compares a reduced row profile and its edges against a recent reference. A phase change needs a distinct correlation peak, agreement
in at least two independent regions with no strongly contradicting region, and confirmation on two consecutive rows. This prevents a changing
legend or grey wedge from steering the line clock. A confirmed correction changes the sampling origin for the current and following rows; earlier
good rows stay in place. Half a line of look-ahead is retained for corrections in either direction and flushed when a start or stop tone arrives.
The weather fax screen reports the recovery count and last correction in pixels.

This restores alignment, not the missing picture content or the number of whole lines lost. A flat or unrelated picture may not provide enough
evidence to recover; in that case the decoder leaves the phase alone. The radio-delivery warning is an estimate based on callback delivery and
is reported after the event, so it is not treated as an exact timestamp or inserted directly into the picture. An ordinary CPU scheduling delay
with every sample retained does not change decoding: callback chunk size and arrival time are not line clocks.

`test_marine_fax_resync` removes actual samples (rather than replacing them with silence), checks recovery for 5–800 ms losses and repeated
samples, and checks that good rows preceding the loss remain unchanged. Existing fax tests cover initial phasing, a picture joined late, clock
slant, manual correction, weak signals, fading, tone detection, concurrent image reads and chunk-size independence.
