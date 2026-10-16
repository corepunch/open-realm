// Read-only shared-owner observations, installed inside the hash-checked path observer.
function installPathRandomMovement(base, hook) {
    const words = (p,n) => Array.from({length:n},(_,i)=>p.add(i*4).readU32());
    const owner = () => base.add(0xd53a48).readPointer();
    const state = () => words(owner(),2);
    const counter = () => owner().add(0x538).readU32();
    hook(0x1702f0, {
        onEnter() {
            this.sep=this.context.ecx;this.mover=this.sep.add(0x14).readPointer();
            this.row={mover:this.mover.toString(),before:words(this.sep.add(0x18),3),
                position:words(this.mover.add(0x78),2),speed:this.mover.add(0xc0).readU32(),ownerBefore:state()};
        },
        onLeave() {
            bump('separation-owner');
            if(counts['separation-owner']<=config.samples)emit('separation-owner',{...this.row,
                after:words(this.sep.add(0x18),3),afterPosition:words(this.mover.add(0x78),2),
                ownerAfter:state(),counter:counter()});
        }
    });
    hook(0x1d19e0, {
        onEnter(){this.out=this.context.ecx;this.before=state();},
        onLeave(){
            bump('overlap-draw');
            if(counts['overlap-draw']<=config.samples)emit('overlap-draw',{
                before:this.before,after:state(),direction:words(this.out,2),counter:counter()});
        }
    });
    hook(0x16f570, {
        onEnter(){this.query=this.context.ecx;},
        onLeave(){
            const count=this.query.add(0x1c).readU32(),data=this.query.add(0xc).readPointer(),members=[];
            if(count>32768)throw new Error('Unexpected separation candidate count');
            for(let i=0;i<count;i++)members.push(data.add(i*8).readPointer().add(0x30).readPointer().toString());
            bump('separation-query');
            if(counts['separation-query']<=config.samples)emit('separation-query',{
                source:this.query.add(0x40).readPointer().toString(),members,
                rectangle:words(this.query.add(0x24),4),point:words(this.query.add(0x44),2),counter:counter()});
        }
    });
}
