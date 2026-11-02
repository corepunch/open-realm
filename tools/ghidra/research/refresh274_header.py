"""Emit frozen original outputs; no recalculation of expectations."""
def header(data):
 out='/* Literal complete169680 outputs, initialized0.33; refresh274_oracle.py. */\n'
 for name in('rows','prediction'):
  rows=data[name];out+='static uint32_t const refresh274_'+name+'[]['+str(len(rows[0]))+']={\n'+''.join('{'+','.join('0x%08xu'%x for x in row)+'},\n'for row in rows)+'};\n'
 return out
