#!/usr/bin/env python3
"""Validate one stock unit profile's read-only getter/mask-publication capture."""
import argparse
import json
from pathlib import Path

GAME_SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def word(value):
    if type(value) is not int or not 0<=value<=0xffffffff:raise ValueError('invalid profile raw word')
    return value


def verify(rows):
    if any(row.get('type')=='error' for row in rows):raise ValueError('profile observer error')
    metadata=[r for r in rows if r.get('event')=='metadata']
    end=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=GAME_SHA or not metadata[0].get('profileEvents'):
        raise ValueError('profile capture metadata differs')
    if len(end)!=1 or not end[0].get('installed'):raise ValueError('profile completion marker missing')
    getters=[r for r in rows if r.get('event')=='movement-profile']
    published=[r for r in rows if r.get('event')=='movement-mask-publication']
    if not getters or not published:raise ValueError('profile observations missing')
    rawcodes={word(r['rawcode']) for r in getters}
    if len(rawcodes)!=1:raise ValueError('stock profile capture requires one unit type')
    profile={}
    for kind in ('category','query-mask'):
        selected=[r for r in getters if r['kind']==kind]
        if len(selected)!=end[0]['counts'].get('profile-'+kind):raise ValueError('truncated profile '+kind)
        values={word(r['value']) for r in selected}
        if len(values)!=1:raise ValueError('profile getter values differ')
        profile[kind]=values.pop()
    if len(getters)!=sum(r['kind'] in profile for r in getters):raise ValueError('unknown profile getter kind')
    if len(published)!=end[0]['counts'].get('movement-mask-publication'):raise ValueError('truncated mask publication')
    expected=(profile['query-mask']&0xffffff)|((profile['query-mask']<<24)&0xffffffff)
    observations=[]
    for row in published:
        if word(row['category'])!=profile['category'] or word(row['queryMask'])!=profile['query-mask']:
            raise ValueError('published mask inputs differ from profile')
        if word(row['objectCategory'])&0xffffff!=profile['category']&0xffffff:
            raise ValueError('fine object category differs')
        if word(row['pathMask'])!=expected:raise ValueError('owned path mask differs')
        if not row.get('mover'):raise ValueError('published mover missing')
        observations.append([row['objectCategory'],row['pathMask']])
    return dict(binary_sha256=GAME_SHA,rawcode=rawcodes.pop(),category=profile['category'],
                query_mask=profile['query-mask'],path_mask=expected,publications=observations,passed=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    result=verify([json.loads(line) for line in args.capture.read_text().splitlines() if line])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
