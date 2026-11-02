/* Retail d51e5680, accepted Payoff287 final1/final3/control final2.
 * Unchanged public values; trajectory and post-nested getters are separate contracts. */
static struct {char const *label;uint32_t order;bool paused;float px,py;bool target;float speed;unsigned defend;} const pause289_rows[20][24]={
    [9]={{"before",0,0,0.000f,0.000f,0,270.000f,1},{"player-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"unit-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",851973,1,0.000f,0.000f,0,270.000f,1},{"nested-move-true",851973,1,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",851973,1,0.000f,0.000f,0,270.000f,1},{"paused",851973,1,0.000f,0.000f,0,270.000f,1},{"waiting",851973,1,0.000f,0.000f,0,270.000f,1},{"unpause-return",851973,0,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"player-point-851986",851986,0,1040.000f,1040.000f,0,270.000f,1},{"unit-point-851986",851986,0,1040.000f,1040.000f,0,270.000f,1},{"resumed",851986,0,0.000f,0.000f,0,270.000f,1}},
    [10]={{"before",0,0,0.000f,0.000f,0,270.000f,1},{"player-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"unit-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",851973,1,0.000f,0.000f,0,270.000f,1},{"nested-stop-true",851973,1,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",851973,1,0.000f,0.000f,0,270.000f,1},{"paused",851973,1,0.000f,0.000f,0,270.000f,1},{"waiting",851973,1,0.000f,0.000f,0,270.000f,1},{"unpause-return",851973,0,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"player-immediate-851972",851972,0,0.000f,0.000f,0,270.000f,1},{"unit-immediate-851972",851972,0,0.000f,0.000f,0,270.000f,1},{"resumed",0,0,0.000f,0.000f,0,270.000f,1}},
    [14]={{"before",0,0,0.000f,0.000f,0,270.000f,1},{"player-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"unit-point-851973",851973,1,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",851973,1,0.000f,0.000f,0,270.000f,1},{"nested-unpause",851973,0,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",851973,0,0.000f,0.000f,0,270.000f,1},{"paused",851973,0,0.000f,0.000f,0,270.000f,1},{"player-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"unit-immediate-852056",852056,0,0.000f,0.000f,0,270.000f,1},{"waiting",0,0,0.000f,0.000f,0,270.000f,1},{"unpause-return",0,0,0.000f,0.000f,0,270.000f,1},{"resumed",0,0,0.000f,0.000f,0,270.000f,1}},
};
/* Read-only native pause-exit user counts, including forced packets. */
static uint32_t const pause289_counts[20]={[9]=3,[10]=3,[14]=2};

/* Read-only ownership markers; original class getters identify Defend/Move. */
static struct {unsigned scene,row;char const *label;int32_t defend[2],move[2],unit;} const pause289_counters[]={
    {9,1,"player-point-851973",{0,0},{0,0},0},
    {9,2,"unit-point-851973",{0,0},{0,0},0},
    {9,3,"player-immediate-852056",{1,0},{0,0},1},
    {9,4,"nested-move-true",{1,0},{0,0},1},
    {9,5,"unit-immediate-852056",{1,0},{0,0},1},
    {9,6,"paused",{1,1},{1,1},1},
    {9,7,"waiting",{1,1},{1,1},1},
    {9,8,"unpause-return",{0,0},{0,0},0},
    {9,9,"player-immediate-852056",{0,0},{0,0},0},
    {9,10,"unit-immediate-852056",{0,0},{0,0},0},
    {9,11,"player-point-851986",{0,0},{0,0},0},
    {9,12,"unit-point-851986",{0,0},{0,0},0},
    {9,13,"resumed",{0,0},{0,0},0},
    {10,1,"player-point-851973",{0,0},{0,0},0},
    {10,2,"unit-point-851973",{0,0},{0,0},0},
    {10,3,"player-immediate-852056",{1,0},{0,0},1},
    {10,4,"nested-stop-true",{1,0},{0,0},1},
    {10,5,"unit-immediate-852056",{1,0},{0,0},1},
    {10,6,"paused",{1,1},{1,1},1},
    {10,7,"waiting",{1,1},{1,1},1},
    {10,8,"unpause-return",{0,0},{0,0},0},
    {10,9,"player-immediate-852056",{0,0},{0,0},0},
    {10,10,"unit-immediate-852056",{0,0},{0,0},0},
    {10,11,"player-immediate-851972",{0,0},{0,0},0},
    {10,12,"unit-immediate-851972",{0,0},{0,0},0},
    {10,13,"resumed",{0,0},{0,0},0},
    {14,1,"player-point-851973",{0,0},{0,0},0},
    {14,2,"unit-point-851973",{0,0},{0,0},0},
    {14,3,"player-immediate-852056",{1,0},{0,0},1},
    {14,4,"nested-unpause",{0,-1},{-1,-1},0},
    {14,5,"unit-immediate-852056",{0,-1},{-1,-1},0},
    {14,6,"paused",{0,0},{0,0},0},
    {14,7,"player-immediate-852056",{0,0},{0,0},0},
    {14,8,"unit-immediate-852056",{0,0},{0,0},0},
    {14,9,"waiting",{0,0},{0,0},0},
    {14,10,"unpause-return",{0,0},{0,0},0},
    {14,11,"resumed",{0,0},{0,0},0},
};
