#!/usr/bin/env python3
"""Validate one stock unit profile's read-only getter/mask-publication capture."""
import argparse
import json
from pathlib import Path

GAME_SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def word(value):
    if type(value) is not int or not 0<=value<=0xffffffff:raise ValueError('invalid profile raw word')
    return value


def verify(rows, fine_objects=False):
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
    result=dict(binary_sha256=GAME_SHA,rawcode=rawcodes.pop(),category=profile['category'],
                query_mask=profile['query-mask'],path_mask=expected,publications=observations,passed=True)
    if fine_objects:
        if not metadata[0].get('velocityEvents') or not metadata[0].get('blockers'):
            raise ValueError('fine object observer options missing')
        commits=[r for r in rows if r.get('event')=='velocity-commit']
        if not commits or len(commits)!=end[0]['counts'].get('velocity-commit'):
            raise ValueError('truncated fine object velocity commits')
        movers={r['mover'] for r in published}
        set_count=clear_count=0
        for r in commits:
            before,after=word(r['fineFlagsBefore']),word(r['fineFlagsAfter'])
            if r['mover'] not in movers or not r.get('fineObject') or r['fineObject']=='0x0':
                raise ValueError('fine object identity missing from profile publication')
            if len(r['after'])!=8:
                raise ValueError('invalid committed velocity words')
            # +88 is the speed cap; actual velocity is mover+80/+84.
            moving=any(word(v) & 0x7fffffff for v in r['after'][4:6])
            if bool(after & 0x20000000)!=moving or (before & 0xdfffffff)!=(after & 0xdfffffff):
                raise ValueError('fine object velocity flag differs from commit')
            set_count+=int(not before & 0x20000000 and moving)
            clear_count+=int(bool(before & 0x20000000) and not moving)
        if not set_count or not clear_count:raise ValueError('fine object moving/idle transitions missing')
        searches=[r for r in rows if r.get('event')=='search' and r.get('kind')=='fine']
        if not searches or len(searches)!=end[0]['counts'].get('fine-search'):
            raise ValueError('truncated fine object searches')
        hits=records=0
        for r in searches:
            stats=r['blockers']
            if stats['omittedHits'] or stats['unclassifiedHits']:raise ValueError('incomplete fine object hits')
            hits+=stats['objectHits']
            for obj in stats['objects'].values():
                if (not obj['isMover'] or obj['payload'] not in movers or obj['flags']!=0 or obj['mode']!=0 or
                        obj['objectMask'] & 0xffffff != profile['category'] or obj['queryMask']!=expected):
                    raise ValueError('fine search blocker differs from published idle profile')
                records+=1
        if profile['category'] & profile['query-mask'] and not hits:
            raise ValueError('fine object overlap lacks an observed idle blocker')
        if not profile['category'] & profile['query-mask'] and hits:
            raise ValueError('fine object nonblocking category produced hits')
        result.update(velocity_commits=len(commits),moving_transitions=set_count,idle_transitions=clear_count,
                      fine_searches=len(searches),object_hits=hits,object_records=records)
    return result



def verify_table(rows):
    """Require complete getters/publications, then check each authored rawcode."""
    if any(row.get('type') == 'error' for row in rows):
        raise ValueError('profile observer error')
    metadata = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or metadata[0].get('sha256') != GAME_SHA or not metadata[0].get('profileEvents'):
        raise ValueError('profile capture metadata differs')
    if len(end) != 1 or not end[0].get('installed'):
        raise ValueError('profile completion marker missing')
    getters = [r for r in rows if r.get('event') == 'movement-profile']
    publications = [r for r in rows if r.get('event') == 'movement-mask-publication']
    if not getters or not publications:
        raise ValueError('profile observations missing')
    for kind in ('category', 'query-mask'):
        if sum(r['kind'] == kind for r in getters) != end[0]['counts'].get('profile-' + kind):
            raise ValueError('truncated profile ' + kind)
    if any(r['kind'] not in ('category', 'query-mask') for r in getters):
        raise ValueError('unknown profile getter kind')
    if len(publications) != end[0]['counts'].get('movement-mask-publication'):
        raise ValueError('truncated mask publication')
    profiles = {}
    for row in getters:
        profile = profiles.setdefault(word(row['rawcode']), {})
        value = word(row['value'])
        if row['kind'] in profile and profile[row['kind']] != value:
            raise ValueError('profile getter values differ')
        profile[row['kind']] = value
    movers = {}
    for row in publications:
        rawcode = word(row['rawcode'])
        profile = profiles.get(rawcode)
        if profile is None or set(profile) != {'category', 'query-mask'}:
            raise ValueError('publication rawcode lacks both getters')
        category, query = profile['category'], profile['query-mask']
        expected = (query & 0xffffff) | ((query << 24) & 0xffffffff)
        if word(row['category']) != category or word(row['queryMask']) != query:
            raise ValueError('published mask inputs differ from profile')
        if word(row['objectCategory']) & 0xffffff != category & 0xffffff or word(row['pathMask']) != expected:
            raise ValueError('published object/path mask differs')
        mover = row.get('mover')
        if not mover or (mover in movers and movers[mover] != rawcode):
            raise ValueError('published mover/rawcode identity differs')
        movers[mover] = rawcode
    result = []
    for rawcode, profile in sorted(profiles.items()):
        if set(profile) != {'category', 'query-mask'} or rawcode not in movers.values():
            raise ValueError('profile lacks paired getters or publication')
        result.append(dict(rawcode=rawcode, category=profile['category'], query_mask=profile['query-mask'],
                           movers=sum(v == rawcode for v in movers.values())))
    return dict(binary_sha256=GAME_SHA, profiles=result, publications=len(publications), passed=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--fine-objects',action='store_true',help='require complete velocity/idle blocker observations')
    parser.add_argument('--table',action='store_true',help='validate multiple authored unit profiles')
    args=parser.parse_args()
    if args.table and args.fine_objects:parser.error('--table does not infer mixed-type blocker coverage')
    rows=[json.loads(line) for line in args.capture.read_text().splitlines() if line]
    result=verify_table(rows) if args.table else verify(rows,args.fine_objects)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
