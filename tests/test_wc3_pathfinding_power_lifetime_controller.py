import io
from pathlib import Path
import sys,tempfile,unittest
from types import SimpleNamespace
from unittest.mock import Mock,patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import wc3_power_lifetime as controller
BEFORE='POWER350 c=1090 P350 tick=20 beat=13 label=before\n'
AFTER='POWER350 c=1090 P350 tick=20 beat=13 label=after\n'
COMPLETE='POWER350 c=1124 P350 tick=30 beat=19 label=complete\n'
def process(lines,alive=False):
 p=SimpleNamespace(pid=4242,stdout=io.StringIO(lines),returncode=None if alive else 0)
 p.poll=lambda:p.returncode
 p.kill=Mock(side_effect=lambda:setattr(p,'returncode',-9))
 p.wait=Mock(side_effect=lambda timeout:p.returncode)
 return p
class LifetimeProcessControllerTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.log=Path(self.tmp.name)/'child.log'
 def run_case(self,p,kind):
  with patch.object(controller.subprocess,'Popen',return_value=p)as spawn:
   result=controller.run_worker('engine','fixtures',kind,'classic',self.log)
   return result,spawn
 def test_reject_existing_log_before_spawning(self):
  self.log.write_text('prior evidence')
  with patch.object(controller.subprocess,'Popen')as spawn:
   with self.assertRaises(ValueError):controller.run_worker('engine','fixtures',0,'classic',self.log)
   spawn.assert_not_called()
 def test_invalid_bounds_never_launch(self):
  with patch.object(controller.subprocess,'Popen')as spawn:
   for hold in (0,-1,float('nan'),float('inf'),61):
    with self.assertRaises(ValueError):controller.run_worker('engine','fixtures',0,'classic',self.log,hold=hold)
   spawn.assert_not_called()
 def test_absent_after_is_not_proof_if_process_exited(self):
  with self.assertRaises(AssertionError):self.run_case(process(BEFORE),0)
 def test_terminating_control_requires_after_and_complete(self):
  with self.assertRaises(AssertionError):self.run_case(process(BEFORE+COMPLETE),3)
 def test_public_admission_counter_is_checked(self):
  with self.assertRaises(AssertionError):self.run_case(process((BEFORE+AFTER+COMPLETE).replace('c=1090','c=1091')),3)
 def test_worker_cannot_inherit_sharding_or_overwrite_parent_junit(self):
  with patch.dict(controller.os.environ,TEST_SHARD='3/4',TEST_JUNIT='parent.xml'):
   result,spawn=self.run_case(process(BEFORE+AFTER+COMPLETE),3)
  self.assertFalse(result['timed_out']);env=spawn.call_args.kwargs['env']
  self.assertNotIn('TEST_SHARD',env);self.assertNotIn('TEST_JUNIT',env)
 def test_holding_worker_is_killed_and_reaped(self):
  p=process(BEFORE,alive=True)
  with patch.object(controller.time,'monotonic',side_effect=[0,1,14]):result,_=self.run_case(p,0)
  self.assertTrue(result['timed_out']);self.assertEqual(result['exit_code'],-9)
  p.kill.assert_called_once();p.wait.assert_called_once_with(timeout=5);self.assertTrue(p.stdout.closed)
 def test_startup_failure_still_kills_owned_worker(self):
  p=process('',alive=True)
  with patch.object(controller.time,'monotonic',side_effect=[0,31]):
   with self.assertRaises(AssertionError):self.run_case(p,0)
  p.kill.assert_called_once();p.wait.assert_called_once_with(timeout=5);self.assertTrue(p.stdout.closed)
if __name__=='__main__':unittest.main()
