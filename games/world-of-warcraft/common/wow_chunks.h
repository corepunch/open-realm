#ifndef WOW_CHUNKS_H
#define WOW_CHUNKS_H

#include "common/shared.h"

/* WoW WMO/ADT/WDT chunk tags are stored reversed on disk: the canonical tag
 * "MOHD" appears as the bytes 'D','H','O','M'. Each ID_* below is named after
 * the on-disk (reversed) byte order so it can be compared DWORD-wise against a
 * raw memcpy of the tag bytes; the trailing comment gives the canonical tag. */

/* WMO */
#define ID_REVM MAKEFOURCC('R','E','V','M') /* MVER — version (WMO + WDT) */
#define ID_PGOM MAKEFOURCC('P','G','O','M') /* MOGP */
#define ID_YPOM MAKEFOURCC('Y','P','O','M') /* MOPY */
#define ID_IVOM MAKEFOURCC('I','V','O','M') /* MOVI */
#define ID_TVOM MAKEFOURCC('T','V','O','M') /* MOVT */
#define ID_RNOM MAKEFOURCC('R','N','O','M') /* MONR */
#define ID_VTOM MAKEFOURCC('V','T','O','M') /* MOTV */
#define ID_ABOM MAKEFOURCC('A','B','O','M') /* MOBA */
#define ID_VCOM MAKEFOURCC('V','C','O','M') /* MOCV */
#define ID_NBOM MAKEFOURCC('N','B','O','M') /* MOBN */
#define ID_RBOM MAKEFOURCC('R','B','O','M') /* MOBR */
#define ID_DHOM MAKEFOURCC('D','H','O','M') /* MOHD */
#define ID_XTOM MAKEFOURCC('X','T','O','M') /* MOTX */
#define ID_TMOM MAKEFOURCC('T','M','O','M') /* MOMT */
#define ID_NDOM MAKEFOURCC('N','D','O','M') /* MODN */
#define ID_SDOM MAKEFOURCC('S','D','O','M') /* MODS */
#define ID_DDOM MAKEFOURCC('D','D','O','M') /* MODD */
#define ID_TLOM MAKEFOURCC('T','L','O','M') /* MOLT */
#define ID_TPOM MAKEFOURCC('T','P','O','M') /* MOPT — portal plane definitions */
#define ID_VPOM MAKEFOURCC('V','P','O','M') /* MOPV — portal vertex array */
#define ID_RPOM MAKEFOURCC('R','P','O','M') /* MOPR — portal references per group */
#define ID_NGOM MAKEFOURCC('N','G','O','M') /* MONG — group name */

/* ADT — root chunks */
#define ID_RDMM MAKEFOURCC('R','D','M','M') /* MHDR — file header (offsets to other chunks) */
#define ID_NICM MAKEFOURCC('N','I','C','M') /* MCIN — 256-entry chunk index */
#define ID_XETM MAKEFOURCC('X','E','T','M') /* MTEX — texture filename string block */
#define ID_FXTM MAKEFOURCC('F','X','T','M') /* MTXF — per-texture flags (cubemap, no-mip, etc.) */
#define ID_XDMM MAKEFOURCC('X','D','M','M') /* MMDX — M2 doodad filename string block */
#define ID_DIMM MAKEFOURCC('D','I','M','M') /* MMID — byte offsets into MMDX */
#define ID_OMWM MAKEFOURCC('O','M','W','M') /* MWMO — WMO filename string block */
#define ID_DIWM MAKEFOURCC('D','I','W','M') /* MWID — byte offsets into MWMO */
#define ID_FDDM MAKEFOURCC('F','D','D','M') /* MDDF — M2 doodad placement records */
#define ID_FDOM MAKEFOURCC('F','D','O','M') /* MODF — WMO placement records */
#define ID_O2HM MAKEFOURCC('O','2','H','M') /* MH2O — WotLK liquid/water layer data */
#define ID_OBMF MAKEFOURCC('O','B','M','F') /* MFBO — flight-bounds height planes */
/* ADT — MCNK subchunks */
#define ID_KNCM MAKEFOURCC('K','N','C','M') /* MCNK — terrain chunk body */
#define ID_RNCM MAKEFOURCC('R','N','C','M') /* MCNR — packed per-vertex normals */
#define ID_TVCM MAKEFOURCC('T','V','C','M') /* MCVT — per-vertex height grid */
#define ID_YLCM MAKEFOURCC('Y','L','C','M') /* MCLY — texture layer records */
#define ID_LACM MAKEFOURCC('L','A','C','M') /* MCAL — alpha blend maps */
#define ID_VCCM MAKEFOURCC('V','C','C','M') /* MCCV — per-vertex baked lighting (WotLK+) */
#define ID_HSCM MAKEFOURCC('H','S','C','M') /* MCSH — 64×64-bit baked shadow map */
#define ID_FRCM MAKEFOURCC('F','R','C','M') /* MCRF — doodad/WMO ref index list */
#define ID_ESCM MAKEFOURCC('E','S','C','M') /* MCSE — ambient sound emitter records */
#define ID_QLCM MAKEFOURCC('Q','L','C','M') /* MCLQ — legacy inline liquid (Vanilla/TBC) */

/* WDT */
#define ID_NIAM MAKEFOURCC('N','I','A','M') /* MAIN */
#define ID_DHPM MAKEFOURCC('D','H','P','M') /* MPHD */

#endif /* WOW_CHUNKS_H */
