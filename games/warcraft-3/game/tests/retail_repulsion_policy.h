/* Generated from frozen SEP-01.3 live observations. Do not edit. */
#ifndef BZ_RETAIL_REPULSION_POLICY_H
#define BZ_RETAIL_REPULSION_POLICY_H
static struct {
    struct { int owner, enabled, selector, group, rank; char const *type; float collision; } units[2];
    bool eligible[2];
} const retail_repulsion_policy[] = {
    {{{0,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* baseline same owner/group/rank */
    {{{0,1,0,0,0,"foot",8},{1,1,0,0,0,"foot",8}},{false,false}}, /* owner 0 vs owner 1 */
    {{{0,1,0,0,0,"foot",8},{0,0,0,0,0,"foot",8}},{false,false}}, /* enabled vs repulse=0 */
    {{{0,1,0,0,0,"foot",8},{0,1,0,1,0,"foot",8}},{false,false}}, /* group 0 vs group 1 */
    {{{0,1,0,1,0,"foot",8},{0,1,0,17,0,"foot",8}},{true,true}}, /* group 1 vs group 17 (alias &15) */
    {{{0,1,0,0,1,"foot",8},{0,1,0,0,0,"foot",8}},{false,true}}, /* rank 1 vs rank 0 */
    {{{0,1,0,0,2,"foot",8},{0,1,0,0,1,"foot",8}},{false,true}}, /* rank 2 vs rank 1 */
    {{{0,1,0,0,17,"foot",8},{0,1,0,0,1,"foot",8}},{true,true}}, /* rank 17 vs rank 1 (alias &15) */
    {{{0,2,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* repulse=2 vs repulse=1 */
    {{{0,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* selector 0 pair at 6 fine */
    {{{0,1,1,0,0,"foot",8},{0,1,1,0,0,"foot",8}},{true,true}}, /* selector 1 pair at 6 fine */
    {{{0,1,2,0,0,"foot",8},{0,1,2,0,0,"foot",8}},{true,true}}, /* selector 2 pair at 6 fine */
    {{{0,1,3,0,0,"foot",8},{0,1,3,0,0,"foot",8}},{true,true}}, /* selector 3 pair at 6 fine */
    {{{0,1,4,0,0,"foot",8},{0,1,4,0,0,"foot",8}},{true,true}}, /* selector 4 pair at 6 fine */
    {{{0,1,5,0,0,"foot",8},{0,1,5,0,0,"foot",8}},{true,true}}, /* selector 5 (zero row) pair */
    {{{0,1,5,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* selector 5 vs selector 0 */
    {{{0,1,17,0,0,"foot",8},{0,1,1,0,0,"foot",8}},{true,true}}, /* selector 17 vs selector 1 (alias &15) */
    {{{0,0,0,0,0,"foot",8},{0,0,0,0,0,"foot",8}},{false,false}}, /* repulse=0 pair */
    {{{0,1,0,0,0,"fly",8},{0,1,0,0,0,"fly",8}},{true,true}}, /* fly vs fly */
    {{{0,1,0,0,0,"fly",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* fly vs foot */
    {{{0,1,0,0,0,"hover",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* hover vs foot */
    {{{0,1,0,0,0,"amph",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* amph vs foot */
    {{{0,1,0,0,0,"horse",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* horse vs foot */
    {{{0,1,0,0,0,"hover",8},{0,1,0,0,0,"hover",8}},{true,true}}, /* hover vs hover */
    {{{0,1,0,0,0,"fly",8},{2,1,0,0,0,"fly",8}},{false,false}}, /* fly owner 0 vs fly owner 2 */
    {{{15,1,0,0,0,"foot",8},{15,1,0,0,0,"foot",8}},{true,true}}, /* owner 15 vs owner 15 */
    {{{15,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{false,false}}, /* owner 15 vs owner 0 */
    {{{15,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{false,false}}, /* owner 15 vs owner 0 + UnitAddAbility('Amec') + SetUnitOwner(1) */
    {{{15,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{false,false}}, /* owner 15 vs owner 0 + SetUnitOwner(1) */
    {{{15,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{false,false}}, /* owner 15 vs owner 0 + 'Amec' + PauseUnit true/false */
    {{{0,1,0,0,0,"foot",8},{1,1,0,0,0,"foot",8}},{false,false}}, /* owner 0 vs owner 1 + SetUnitOwner(0) */
    {{{0,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* channel (ANcl clone) on second unit, collapse */
    {{{0,1,0,0,0,"foot",8},{0,1,0,0,0,"foot",8}},{true,true}}, /* PauseUnit on second unit, collapse, unpause */
    {{{0,1,0,0,0,"amph",8},{0,1,0,0,0,"amph",8}},{true,true}}, /* amph vs amph */
    {{{0,1,0,0,0,"horse",8},{0,1,0,0,0,"horse",8}},{true,true}}, /* horse vs horse */
    {{{0,1,0,0,0,"foot",32.0},{0,1,0,0,0,"foot",8}},{true,true}}, /* radius 1.0 vs radius 0.25 at 0.25 fine */
};
static uint32_t const retail_inert_pairs[][ 12 ] = {
    {0x41800000u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x00000000u,0x00000000u},
    {0x41800000u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x3dcccccdu,0xbe4ccccdu},
    {0x41800000u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x00000000u,0x00000000u},
    {0x41800000u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x3dcccccdu,0xbe4ccccdu},
    {0x41800106u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x00000000u,0x00000000u},
    {0x41800106u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x3dcccccdu,0xbe4ccccdu},
    {0x417ffef7u,0x418000e2u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x00000000u,0x00000000u},
    {0x417ffef7u,0x418000e2u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xb423bdabu,0x0870c038u,0x3dcccccdu,0xbe4ccccdu},
    {0x41820000u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x41820000u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x417dfb0au,0x4181b9f7u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x417dfb0au,0x4181b9f7u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x41880000u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x41880000u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x4177ec26u,0x4186e7dau,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x4177ec26u,0x4186e7dau,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x41a73333u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x41a73333u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x41586b89u,0x41a1d67bu,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x41586b89u,0x41a1d67bu,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x41cf3333u,0x41800000u,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x41cf3333u,0x41800000u,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
    {0x4130084au,0x41c45dbeu,0x41800000u,0x41800000u,0x00000000u,0x00000000u,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x00000000u,0x00000000u},
    {0x4130084au,0x41c45dbeu,0x41800000u,0x41800000u,0x3dcccccdu,0xbe4ccccdu,0xfeb77594u,0x0c7cd854u,0xfeb77594u,0x0c7cd854u,0x3dcccccdu,0xbe4ccccdu},
};
static uint32_t const retail_inert_tails[][ 6 ] = {
    {0x00000000u,0x80000000u,0x00050000u,0x00000000u,0x00000000u,0x00050000u},
    {0x00000000u,0x80000000u,0xabc50003u,0x00000000u,0x00000000u,0xabc50003u},
    {0x3b449ba6u,0xbb83126fu,0x00050000u,0x00000000u,0x00000000u,0x00050000u},
    {0x3b449ba6u,0xbb83126fu,0xabc50003u,0x00000000u,0x00000000u,0xabc50003u},
    {0x3e3851ecu,0xbe75c28fu,0x00050000u,0x00000000u,0x00000000u,0x00050000u},
    {0x3e3851ecu,0xbe75c28fu,0xabc50003u,0x00000000u,0x00000000u,0xabc50003u},
    {0x40400000u,0xc0800000u,0x00050000u,0x00000000u,0x00000000u,0x00050000u},
    {0x40400000u,0xc0800000u,0xabc50003u,0x00000000u,0x00000000u,0xabc50003u},
};
#endif
