import concurrent.futures,subprocess,pathlib,json,hashlib
r=pathlib.Path(__file__).resolve().parent
sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in r.iterdir() if p.is_file()}
assert all(hashlib.sha256((r/k).read_bytes()).hexdigest()==v for k,v in json.loads((r/'frozen-sources.json').read_text()).items())
def run(mode):
 cmd=['/GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.sh','/GitHub/wc3-analysis/verify-venv/bin/python',str(r/'itemdrop_capture.py'),'--data','{DATA}','--map','Maps/TargetCouplingInstant193Callbacks.w3m','--install-map',str(r/'TargetCouplingInstant193Callbacks.w3m'),'--preload','target-coupling-instant193callbacks-stock.txt','--start-file','target-coupling-instant193callbacks-stock-start.txt','--marker-prefix','CPI ','--mode','control' if mode=='control1' else 'observe','--remote','{REMOTE}','--x11-display','{DISPLAY}','--env','{ENV}','--seconds','210','--continue-at','30','--output',str(r/(mode+'.jsonl'))]
 if mode=='control1':cmd.append('--control-continue-once')
 with (r/(mode+'.log')).open('x') as f: result=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 print(mode,result.returncode,flush=True);return result.returncode
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,['observe1','control1','observe2']))
raise SystemExit(any(results))
