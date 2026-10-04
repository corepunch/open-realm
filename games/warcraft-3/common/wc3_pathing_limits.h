#ifndef BZ_WC3_PATHING_LIMITS_H
#define BZ_WC3_PATHING_LIMITS_H

#define BZ_WC3_GROUP_ORDER_UNITS 12 // original CGroup point-order admission, independent of JASS group capacity
#define BZ_WC3_FINE_WORK 2048 // queue attempts/request; retain the engine's bounded synchronous routing budget
#define BZ_WC3_GROUP_ACC_WORK 5000 // original1678f0; group path is distinct from the member path
#define BZ_WC3_UNIT_ACC_WORK 400 // original166060 path+86; ordinary owned-path adaptive request
#define BZ_WC3_FINE_OWNER_WORK 1100 // queue attempts/ordinary owner bucket; strict greater-than veto in168310
#define BZ_WC3_FINE_REQUEST_INTERVAL 10u // owner visits; original168910 minimum interval before fine retries
#define BZ_WC3_FINE_FAST_WORK 64u // cumulative attempts; original166e90 clears the interval below this bucket work
#define BZ_WC3_PATH_OWNER_START 0x400u // visits; original157610 counter origin and15aa80 unsigned-wrap reload
#define BZ_WC3_UNIT_FINE_WORK 700 // original166060 path+84; actual denied next iteration is also charged
#define BZ_WC3_FINE_NODES 32768 // original147af0 refuses a new identity at0x8000; existing nodes still resolve
#define BZ_WC3_FINE_NODE_GROW 2048 // original147600 node table growth, independent of search work
#define BZ_WC3_FINE_HEAP_GROW 1024 // original147600 open table growth; includes slot0 sentinel
#define BZ_WC3_ACC_HEAP_GROW 2048 // original14f570 adaptive open table; includes slot0 sentinel
#define BZ_WC3_ACC_ROUTE_NODES 131072 // ushort parent chain plus one portal sentinel per node
#define BZ_WC3_FINE_HASH 65536 // sparse lookup; at most half full at the native node limit

#endif
