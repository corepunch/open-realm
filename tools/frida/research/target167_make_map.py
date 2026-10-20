#!/usr/bin/env python3
"""Build the public target-loss subscription-order probe with the shared arena."""
import target021_make_map as builder
builder.PROBES['target167']=('target167_probe.j','rs-target167.txt',3)
if __name__=='__main__':
    builder.main()
