"""Execute current Classic/TFT regression suites independently of frozen RE verifiers."""
import re

def run_engine(binary,data,report,patterns,timeout=180):
 import os,subprocess,xml.etree.ElementTree as ET
 results={}
 for edition in('classic','tft'):
  tests=assertions=0
  for i,name in enumerate(patterns):
   log=report.with_name(report.stem+'-'+edition+'-'+str(i)+'.log');junit=log.with_suffix('.xml')
   env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
   args=[str(binary.resolve()),'-data',str(data.resolve())]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test',name]
   with log.open('w')as f:subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,check=True)
   suite=ET.parse(junit).getroot();totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
   if len(totals)!=1 or totals[0][0]!=totals[0][1]or int(totals[0][2])<1 or any(suite.attrib[k]!='0'for k in('failures','errors','skipped')):raise ValueError('missing/failed queue engine test')
   tests+=int(totals[0][2]);assertions+=int(totals[0][0])
  results[edition]=dict(tests=tests,assertions=assertions)
 return results
