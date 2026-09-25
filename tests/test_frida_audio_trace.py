#!/usr/bin/env python3
"""Regression checks for trace evidence attribution, without retail assets/Frida."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('audio_trace', Path(__file__).resolve().parents[1] / 'tools/frida/analyze_audio_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def event(kind, seq, **fields):
    return dict(event=kind, seq=seq, ticks=seq, thread=1, **fields)


def call(seq, label, instance, channel=1, result=0):
    sound = dict(address=instance, file='shared.wav', user=24, channel=channel, priority=1000, flags=1024)
    return [event('label-request', seq, id=seq, label=label),
            event('variant-before', seq+1, row=dict(variant=0)),
            event('variant-after', seq+2, row=dict(variant=1)),
            event('admit-result', seq+3, result=result, sound=sound),
            event('label-result', seq+4, id=seq, result=2 if result else 0, row=dict(variant=0 if result else 1))]


class AudioTraceTests(unittest.TestCase):
    def test_empty_capture_cannot_supply_evidence(self):
        result = trace.analyze([])
        for key in ('variant_rollbacks', 'rejected_what_counts', 'shared_file_policies', 'playback_notifications'):
            self.assertEqual(result[key], [])

    def test_rejection_rollback_and_counter_are_checked(self):
        events = [event('what', 1, unit='0x18', count=2), *call(2, 'HeroWhat', '0x100', result=3),
                  event('what-done', 7, unit='0x18', count=2)]
        result = trace.analyze(events)
        self.assertEqual(len(result['variant_rollbacks']), 1)
        self.assertEqual(len(result['rejected_what_counts']), 1)
        self.assertEqual(result['violations'], [])
        events[-2]['row']['variant'] = 1
        events[-1]['count'] = 3
        self.assertEqual(len(trace.analyze(events)['violations']), 2)

    def test_notifications_match_instances_not_latest_request_for_user(self):
        events = [*call(1, 'AliasWhat', '0x100'), *call(6, 'AliasReady', '0x200', channel=4)]
        for kind, seq, slot in [('portrait-start', 11, 0), ('portrait-end', 15, 1)]:
            events.append(event(kind, seq, notification=dict(unit='0x18'),
                                callback=dict(slot=slot, sound=dict(address='0x100'))))
        events.append(event('cooldown-set', 16, unit='0x18'))
        result = trace.analyze(events)
        self.assertEqual(result['playback_notifications'][0]['label'], 'AliasWhat')
        self.assertEqual(result['playback_notifications'][0]['admission_seq'], 4)
        self.assertEqual(len(result['shared_file_policies']), 1)
        events[-2]['callback']['slot'] = 3
        self.assertEqual(trace.analyze(events)['playback_notifications'][0]['end_slot'], 3)
        events[-2]['callback']['slot'] = 2
        self.assertEqual(len(trace.analyze(events)['violations']), 1)
        events[8]['sound']['file'] = 'other.wav'
        self.assertEqual(trace.analyze(events)['shared_file_policies'], [])


if __name__ == '__main__': unittest.main()
