#!/usr/bin/env python3
# Draws three look options for the Partials window as HTML, then Chrome prints them to PNG.
import os, subprocess, html
HERE = os.path.dirname(os.path.abspath(__file__))

SECT = {
 'TIMBRE':   [('mix',.0,'0%'),('phase',.5,'180°'),('rand',.5,'0%'),('sub 1',.0,'0%'),('sub 2',.0,'0%'),('sub 3',.0,'0%'),('prot',.0,'0%'),('clip',1,'off'),('env',.8,'0 dB'),('vol',.8,'0 dB'),('gain',.0,'0%')],
 'PITCH':    [('freq',.1,'1.000/1'),('detune',.1,'1.000/1'),('pitch',.5,'0 ct'),('vibrato',.0,'0 ct'),('speed',.4,'5 Hz'),('glide',.5,'off'),('limit',.0,'off')],
 'UNISON':   [('order',.33,'4'),('pan',.5,'50%'),('pitch',.35,'35%'),('phase',1,'blur'),('alt',.0,'0%')],
 'FILTER 1': [('env',.5,'0%'),('freq',.62,'1.4 kHz'),('kb',.5,'0%'),('width',.5,'50%'),('res',.3,'30%'),('r.width',.5,'50%'),('offset',.5,'0 ct'),('osc',.0,'0%')],
 'FILTER 2': [('env',.5,'0%'),('freq',1,'20 kHz'),('kb',.5,'0%'),('width',.5,'50%'),('res',.0,'0%'),('r.width',.5,'50%'),('offset',.5,'0 ct'),('osc',.0,'0%')],
 'BLUR':     [('mix',1,'100%'),('harm',.0,'0%'),('top',.5,'50%'),('bottom',.5,'50%'),('attack',.0,'0%'),('decay',.0,'0%')],
 'PLUCK':    [('time',.45,'45%')],
 'PHASER':   [('mix',.0,'0%'),('width',.5,'50%'),('offset',.0,'0%'),('speed',.5,'still'),('kb',.5,'0%')],
 'PRISM':    [('amount',.5,'0%')],
 'HARMONIZER':[('amount',.0,'0%'),('width',.5,'50%'),('strength',1,'100%'),('shift','0+1',''),('gap','0x1','')],
 'TREMOLO':  [('speed',.5,'500 ms'),('gap',.25,'90°'),('depth',.0,'0%')],
 'STRUM':    [('time',.0,'0%'),('tension',.5,'0%')],
}
MENUS = {'TIMBRE':'Fade','UNISON':'Classic','FILTER 1':'Classic low pass','FILTER 2':'Crude low pass','PHASER':'Classic','PRISM':'add','PITCH':'Hz','STRUM':'up'}

SPEC_JS = r'''
function spec(id, pal, vertical){
  const c=document.getElementById(id), r=c.getBoundingClientRect(), d=2;
  c.width=r.width*d; c.height=r.height*d; const g=c.getContext('2d'); const W=c.width,H=c.height;
  g.fillStyle=pal.bg; g.fillRect(0,0,W,H);
  const T=vertical?H:W, F=vertical?W:H, fmin=Math.log2(30), fmax=Math.log2(20000);
  const notes=[[0,.30,110],[.30,.52,146.8],[.52,.70,98],[.70,1.01,130.8]];
  let seed=7; const rnd=()=>{seed=(seed*16807)%2147483647; return seed/2147483647;};
  for(const [t0,t1,f0] of notes){
    for(let n=1;n<=400;n++){ const f=f0*n; if(f>19000) break;
      const y=(Math.log2(f)-fmin)/(fmax-fmin); const jit=rnd();
      for(let x=Math.floor(t0*T); x<t1*T; x+=2){ const t=(x/T-t0)/(t1-t0);
        const cut=Math.log2(300)+3.6*Math.exp(-t*3.2)+0.9*Math.sin(t*9+f0)*.25;
        const dd=Math.log2(f)-cut; let a=dd<0?1:Math.exp(-dd*dd*5); a+=1.4*Math.exp(-dd*dd*40);
        a*=Math.min(1,t*40)*(0.55+0.45/Math.sqrt(n))*(0.85+0.15*jit); if(a<.03) continue;
        g.globalAlpha=Math.min(1,a*.8); g.fillStyle=a>1.1?pal.hot:pal.fg;
        const p=Math.max(1,d*(n<24?1.2:.8));
        if(vertical) g.fillRect((y)*F, H-x-2, p, 2); else g.fillRect(x, H-y*F-p, 2, p);
      }}}
  g.globalAlpha=1; g.fillStyle=pal.grid; g.font=(10*d)+'px '+pal.font;
  for(const [hz,l] of [[100,'100'],[1000,'1k'],[10000,'10k']]){ const y=(Math.log2(hz)-fmin)/(fmax-fmin);
    if(vertical){ g.fillRect(y*F,0,1,H); g.fillText(l,y*F+4*d,12*d);} else { g.fillRect(0,H-y*F,W,1); g.fillText(l,4*d,H-y*F-3*d);} }
}
function env(id, pal){ const c=document.getElementById(id), r=c.getBoundingClientRect(), d=2; c.width=r.width*d;c.height=r.height*d;
  const g=c.getContext('2d'),W=c.width,H=c.height; g.fillStyle=pal.bg; g.fillRect(0,0,W,H);
  g.strokeStyle=pal.grid; g.lineWidth=1; for(let i=1;i<16;i++){g.globalAlpha=i%4?0.35:0.8; g.beginPath();g.moveTo(i*W/16,0);g.lineTo(i*W/16,H);g.stroke();}
  for(let i=1;i<4;i++){g.beginPath();g.moveTo(0,i*H/4);g.lineTo(W,i*H/4);g.stroke();} g.globalAlpha=1;
  const P=[[.0,.98],[.04,.06],[.22,.5],[.5,.62],[.72,.97]]; g.beginPath(); g.moveTo(0,H);
  for(let i=0;i<P.length-1;i++){ const a=P[i],b=P[i+1]; for(let t=0;t<=1;t+=.02){ const k=i==1?1-Math.pow(1-t,2.6):(i==3?t*t:t); g.lineTo((a[0]+(b[0]-a[0])*t)*W,(a[1]+(b[1]-a[1])*k)*H);} }
  g.lineTo(P[4][0]*W,H); g.closePath(); g.globalAlpha=.14; g.fillStyle=pal.fg; g.fill(); g.globalAlpha=1;
  g.beginPath(); for(let i=0;i<P.length-1;i++){ const a=P[i],b=P[i+1]; for(let t=0;t<=1;t+=.02){ const k=i==1?1-Math.pow(1-t,2.6):(i==3?t*t:t); const X=(a[0]+(b[0]-a[0])*t)*W,Y=(a[1]+(b[1]-a[1])*k)*H; (i==0&&t==0)?g.moveTo(X,Y):g.lineTo(X,Y);} }
  g.strokeStyle=pal.fg; g.lineWidth=1.5*d; g.stroke();
  for(const p of P){ g.fillStyle=pal.bg; g.strokeStyle=pal.fg; g.lineWidth=1.2*d; g.beginPath(); g.rect(p[0]*W-3*d+ (p[0]==0?3*d:0),p[1]*H-3*d,6*d,6*d); g.fill(); g.stroke(); }
}
function wave(id,pal,sq){ const c=document.getElementById(id); if(!c) return; const r=c.getBoundingClientRect(), d=2; c.width=r.width*d;c.height=r.height*d;
  const g=c.getContext('2d'),W=c.width,H=c.height; g.fillStyle=pal.bg; g.fillRect(0,0,W,H); g.strokeStyle=pal.fg; g.lineWidth=1.3*d; g.beginPath();
  for(let x=0;x<=W;x++){ const t=x/W; let y=0; for(let n=1;n<60;n+=sq?2:1) y+=Math.sin(2*Math.PI*n*t)/n; y*=sq?.6:.5; const Y=H/2-y*H*.36; x?g.lineTo(x,Y):g.moveTo(x,Y);} g.stroke(); }
'''

def page(name, css, body, pal):
    doc = f'''<!doctype html><meta charset="utf-8"><title>{name}</title><style>
*{{box-sizing:border-box;margin:0;padding:0}} html,body{{width:1440px;height:900px;overflow:hidden}}
{css}</style><body>{body}<script>{SPEC_JS}
const pal={pal}; spec('spec',pal,pal.vertical); env('env',pal); wave('w1',pal,false); wave('w2',pal,true);
</script></body>'''
    p = os.path.join(HERE, name + '.html'); open(p, 'w').write(doc); return p

def tabs(active='ENV'):
    return ''.join(f'<span class="tab{" on" if t==active else ""}">{t}</span>' for t in ['ENV','IMG','FX','ADV'])

# ---------- A: paper. Rows of thin bars, spectrogram as a tall column on the right ----------
def rowsA(sec):
    out = f'<div class="sec"><div class="h"><b>{sec}</b>' + (f'<i>{MENUS[sec]} ▾</i>' if sec in MENUS else '') + '</div>'
    for l, v, t in SECT[sec]:
        if isinstance(v, str): out += f'<div class="r"><span>{l}</span><em class="dial">{v}</em><u></u></div>'
        else: out += f'<div class="r"><span>{l}</span><div class="bar"><div style="width:{v*100:.0f}%"></div></div><u>{t}</u></div>'
    return out + '</div>'
cssA = '''body{background:#f4f2ec;color:#16161a;font:11px/1.2 "Helvetica Neue",Helvetica,Arial,sans-serif}
.top{height:44px;display:flex;align-items:center;padding:0 20px;border-bottom:1px solid #16161a;gap:22px}
.top h1{font-size:17px;font-weight:500;letter-spacing:.02em}.top .sp{flex:1}.top span{color:#666}.top b{font-weight:500;color:#16161a}
.ab{border:1px solid #16161a;padding:3px 9px}.ab.on{background:#16161a;color:#f4f2ec}
.main{display:grid;grid-template-columns:repeat(5,196px) 1fr;height:536px;border-bottom:1px solid #16161a}
.col{border-right:1px solid #cfccc2;padding:10px 14px;display:flex;flex-direction:column;gap:12px}
.sec .h{display:flex;justify-content:space-between;border-bottom:1px solid #16161a;padding-bottom:3px;margin-bottom:5px}
.sec .h b{font-weight:600;letter-spacing:.08em;font-size:10px}.sec .h i{font-style:normal;color:#444}
.r{display:grid;grid-template-columns:50px 1fr 50px;align-items:center;height:16px;gap:6px}.r span{color:#555}.r u{text-decoration:none;text-align:right;font-variant-numeric:tabular-nums}
.bar{height:3px;background:#d8d5cb;position:relative}.bar div{height:3px;background:#16161a;position:relative}.bar div:after{content:"";position:absolute;right:-1px;top:-4px;width:2px;height:11px;background:#16161a}
.dial{font-style:normal;border:1px solid #16161a;padding:0 5px;justify-self:start}
.wv{display:flex;gap:6px;margin-bottom:6px}.wv canvas{width:80px;height:44px;border:1px solid #cfccc2}
#spec{width:100%;height:100%;display:block}.specbox{position:relative}.specbox label{position:absolute;right:10px;bottom:8px;color:#888;font-size:10px;letter-spacing:.08em}
.bot{display:grid;grid-template-columns:230px 1fr 290px;height:320px}
.bot>div{padding:10px 14px}.bot .mid{padding:0;border-left:1px solid #cfccc2;border-right:1px solid #cfccc2;display:flex;flex-direction:column}
.tab{display:inline-block;padding:3px 10px;border:1px solid #16161a;margin-right:-1px;font-size:10px;letter-spacing:.08em}.tab.on{background:#16161a;color:#f4f2ec}
.mid .bar2{height:30px;display:flex;align-items:center;gap:14px;padding:0 12px;border-bottom:1px solid #cfccc2}.mid .bar2 span{color:#555}.mid .bar2 b{font-weight:500}
#env{flex:1 1 0;width:100%;height:0;min-height:0}
.list div{height:17px;display:flex;justify-content:space-between;color:#555}.list .on{color:#16161a;font-weight:600}.list{margin-top:10px}'''
def bodyA():
    cols = [['TIMBRE','PLUCK'],['PITCH','UNISON','PRISM'],['FILTER 1','BLUR'],['FILTER 2','PHASER'],['HARMONIZER','TREMOLO','STRUM']]
    b = '<div class="top"><h1>Partials</h1><span>preset <b>Default ▾</b></span><span class="sp"></span><span class="ab on">A</span><span class="ab">B</span><span>mix <b>50%</b></span><span>X <b>0</b> Y <b>0</b> Z <b>0</b></span><span>lfo <b>100%</b></span><span>volume <b>0 dB</b></span></div><div class="main">'
    for i, c in enumerate(cols):
        b += '<div class="col">' + ('<div class="wv"><canvas id="w1"></canvas><canvas id="w2"></canvas></div>' if i == 0 else '')
        b += ''.join(rowsA(s) for s in c) + '</div>'
    b += '<div class="specbox"><canvas id="spec"></canvas><label>PARTIALS · oct</label></div></div>'
    lst = ''.join(f'<div class="{"on" if i==1 else ""}"><span>{n}</span><span>{"●" if i in (1,4,24) else ""}</span></div>' for i, n in enumerate(['Panning','Volume','FX dry / wet','Timbre mix','Filter 1 frequency','Filter 2 frequency','Filter 1 width','Filter 2 width','Filter 1 resonance','Filter 2 resonance','Pluck amount','Phaser mix','Prism amount','Pitch','Unison pitch']))
    b += f'<div class="bot"><div>{tabs()}<div class="list">{lst}</div></div><div class="mid"><div class="bar2"><b>Volume</b><span>part</span><b>Envelope ▾</b><span>smooth</span><b>0 ms</b><span style="flex:1"></span><span>tempo ○</span><span>global ○</span><span>snap ●</span></div><canvas id="env"></canvas></div><div>'
    b += rowsA2('ENVELOPE', [('attack',.1,'12 ms'),('decay',.4,'310 ms'),('sustain',.5,'50%'),('release',.45,'420 ms')]) + '<div style="height:14px"></div>' + rowsA2('LFO', [('speed',.5,'1/4'),('tension',.32,'32%'),('skew',.5,'0%'),('pw',.5,'0%')]) + '</div></div>'
    return b
def rowsA2(name, rows):
    out = f'<div class="sec"><div class="h"><b>{name}</b></div>'
    for l, v, t in rows: out += f'<div class="r"><span>{l}</span><div class="bar"><div style="width:{v*100:.0f}%"></div></div><u>{t}</u></div>'
    return out + '</div>'

# ---------- B: terminal. Dark, monospace, spectrogram as a wide band along the top ----------
def cellsB(sec, rows=None):
    out = f'<div class="sec"><div class="h">{sec}' + (f'<i>[{MENUS[sec]}]</i>' if sec in MENUS else '') + '</div><div class="cells">'
    for l, v, t in (rows or SECT[sec]):
        if isinstance(v, str): out += f'<div class="c"><span>{l}</span><b>{v}</b><div class="m"><div style="width:30%"></div></div></div>'
        else: out += f'<div class="c"><span>{l}</span><b>{t}</b><div class="m"><div style="width:{v*100:.0f}%"></div></div></div>'
    return out + '</div></div>'
cssB = '''body{background:#0c0d0e;color:#c9cdc8;font:11px/1.25 "SF Mono",Menlo,Monaco,monospace}
.top{height:34px;display:flex;align-items:center;padding:0 16px;gap:22px;border-bottom:1px solid #2a2d2e}.top h1{font-size:12px;font-weight:600;letter-spacing:.18em;color:#e9ece6}.top .sp{flex:1}.top span{color:#6f7572}.top b{color:#e9ece6;font-weight:400}.top .on{color:#0c0d0e;background:#b7e36a;padding:1px 6px}
.specbox{height:250px;border-bottom:1px solid #2a2d2e;position:relative}#spec{width:100%;height:100%;display:block}.specbox label{position:absolute;right:12px;top:8px;color:#6f7572}
.main{display:grid;grid-template-columns:repeat(4,1fr);height:346px;border-bottom:1px solid #2a2d2e}
.col{border-right:1px solid #1d2021;padding:8px 12px;display:flex;flex-direction:column;gap:9px}
.sec .h{color:#b7e36a;letter-spacing:.14em;font-size:10px;margin-bottom:4px;display:flex;justify-content:space-between}.sec .h i{font-style:normal;color:#c9cdc8;letter-spacing:0}
.cells{display:grid;grid-template-columns:repeat(4,1fr);gap:5px 10px}.c span{color:#6f7572;display:block;font-size:10px}.c b{font-weight:400;color:#e9ece6;display:block;white-space:nowrap}
.m{height:2px;background:#24282a;margin-top:2px}.m div{height:2px;background:#b7e36a}
.wv{display:flex;gap:8px}.wv canvas{width:120px;height:40px;border:1px solid #2a2d2e}
.bot{display:grid;grid-template-columns:1fr 330px;height:270px}.mid{display:flex;flex-direction:column;border-right:1px solid #2a2d2e}
.bar2{height:28px;display:flex;align-items:center;gap:14px;padding:0 12px;border-bottom:1px solid #1d2021}.bar2 span{color:#6f7572}.bar2 b{font-weight:400;color:#e9ece6}
.tab{padding:2px 8px;color:#6f7572}.tab.on{color:#0c0d0e;background:#b7e36a}#env{flex:1 1 0;width:100%;height:0;min-height:0}.side{padding:8px 12px;display:flex;flex-direction:column;gap:10px}'''
def bodyB():
    cols = [['TIMBRE','PLUCK','PRISM'],['PITCH','UNISON','TREMOLO'],['FILTER 1','FILTER 2'],['BLUR','PHASER','HARMONIZER']]
    b = '<div class="top"><h1>PARTIALS</h1><span>preset <b>Default</b></span><span class="sp"></span><span class="on">A</span><span>B</span><span>mix <b>50%</b></span><span>x <b>0.00</b> y <b>0.00</b> z <b>0.00</b></span><span>lfo <b>100%</b></span><span>vol <b>0.0 dB</b></span></div>'
    b += '<div class="specbox"><canvas id="spec"></canvas><label>partials · oct · 516</label></div><div class="main">'
    for i, c in enumerate(cols):
        b += '<div class="col">' + ''.join(cellsB(s) for s in c) + ('<div class="wv"><canvas id="w1"></canvas><canvas id="w2"></canvas></div>' if i == 0 else '') + '</div>'
    b += f'</div><div class="bot"><div class="mid"><div class="bar2">{tabs()}<span>target</span><b>[Volume]</b><span>part</span><b>[Envelope]</b><span>smooth</span><b>0 ms</b><span style="flex:1"></span><span>tempo -</span><span>global -</span><span>snap +</span></div><canvas id="env"></canvas></div><div class="side">'
    b += cellsB('ENVELOPE', [('attack',.1,'12 ms'),('decay',.4,'310 ms'),('sustain',.5,'50%'),('release',.45,'420 ms')]) + cellsB('LFO', [('speed',.5,'1/4'),('tension',.32,'32%'),('skew',.5,'0%'),('pw',.5,'0%')]) + cellsB('STRUM') + '</div></div>'
    return b

# ---------- C: panel. Grey, small plain knobs, spectrogram large in the middle ----------
def knob(l, v, t):
    if isinstance(v, str): return f'<div class="k"><div class="dial">{v}</div><span>{l}</span></div>'
    a = -135 + 270 * v
    return f'<div class="k"><svg viewBox="0 0 30 30"><circle cx="15" cy="15" r="12" fill="#f3f3f1" stroke="#1c1c1c" stroke-width="1.2"/><line x1="15" y1="15" x2="15" y2="4.5" stroke="#1c1c1c" stroke-width="1.8" transform="rotate({a:.0f} 15 15)"/></svg><span>{l}</span></div>'
def knobsC(sec, rows=None):
    return f'<div class="sec"><div class="h"><b>{sec}</b>' + (f'<i>{MENUS[sec]} ▾</i>' if sec in MENUS else '') + '</div><div class="ks">' + ''.join(knob(*r) for r in (rows or SECT[sec])) + '</div></div>'
cssC = '''body{background:#d9d9d6;color:#1c1c1c;font:10px/1.2 "Helvetica Neue",Helvetica,Arial,sans-serif}
.top{height:40px;display:flex;align-items:center;padding:0 16px;gap:20px;background:#1c1c1c;color:#d9d9d6}.top h1{font-size:14px;font-weight:700;letter-spacing:.12em}.top .sp{flex:1}.top b{font-weight:500;color:#fff}
.ab{border:1px solid #d9d9d6;padding:2px 8px}.ab.on{background:#ff5a1f;border-color:#ff5a1f;color:#fff}
.main{display:grid;grid-template-columns:368px 1fr 368px;height:560px}
.side{padding:8px 12px;display:flex;flex-direction:column;gap:8px}
.sec{background:#e9e9e6;border-radius:3px;padding:6px 8px}.sec .h{display:flex;justify-content:space-between;margin-bottom:4px}.sec .h b{letter-spacing:.1em;font-size:9.5px}.sec .h i{font-style:normal;background:#fff;padding:0 5px;border-radius:2px}
.ks{display:flex;flex-wrap:wrap;gap:3px 2px}.k{width:41px;text-align:center}.k svg{width:25px;height:25px;display:block;margin:0 auto}.k span{color:#444;font-size:9px}
.dial{height:25px;line-height:25px;font-weight:600;font-size:11px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.specbox{background:#111;position:relative;margin:8px 0}#spec{width:100%;height:100%;display:block}.specbox label{position:absolute;right:10px;top:8px;color:#888;letter-spacing:.1em;font-size:9px}
.wv{position:absolute;left:10px;top:8px;display:flex;gap:6px}.wv canvas{width:92px;height:44px;border:1px solid #444}
.bot{display:grid;grid-template-columns:1fr 368px;height:300px;border-top:2px solid #1c1c1c}.mid{display:flex;flex-direction:column}
.bar2{height:30px;display:flex;align-items:center;gap:12px;padding:0 12px;background:#cfcfcb}.bar2 span{color:#555}.bar2 b{font-weight:600}
.tab{padding:3px 10px;background:#bdbdb8;margin-right:2px;letter-spacing:.08em;border-radius:2px}.tab.on{background:#ff5a1f;color:#fff}#env{flex:1 1 0;width:100%;height:0;min-height:0}'''
def bodyC():
    b = '<div class="top"><h1>PARTIALS</h1><span>preset <b>Default ▾</b></span><span class="sp"></span><span class="ab on">A</span><span class="ab">B</span><span>mix <b>50%</b></span><span>X <b>0</b> Y <b>0</b> Z <b>0</b></span><span>lfo <b>100%</b></span><span>volume <b>0 dB</b></span></div><div class="main"><div class="side">'
    b += knobsC('TIMBRE') + knobsC('PITCH') + knobsC('UNISON') + '<div class="two">' + knobsC('PRISM') + knobsC('PLUCK') + '</div>' + knobsC('TREMOLO') if False else knobsC('TIMBRE') + knobsC('PITCH') + knobsC('UNISON') + '<div class="two">' + knobsC('PRISM') + knobsC('PLUCK') + '</div>'
    b += '</div><div class="specbox"><canvas id="spec"></canvas><div class="wv"><canvas id="w1"></canvas><canvas id="w2"></canvas></div><label>PARTIALS · OCT</label></div><div class="side">'
    b += knobsC('FILTER 1') + knobsC('FILTER 2') + knobsC('BLUR') + knobsC('PHASER') + knobsC('HARMONIZER') + '</div></div>'
    b += f'<div class="bot"><div class="mid"><div class="bar2">{tabs()}<span>target</span><b>Volume ▾</b><span>part</span><b>Envelope ▾</b><span>smooth</span><b>0 ms</b><span style="flex:1"></span><span>tempo ○</span><span>global ○</span><span>snap ●</span></div><canvas id="env"></canvas></div><div class="side">'
    b += knobsC('ENVELOPE', [('attack',.1,''),('decay',.4,''),('sustain',.5,''),('release',.45,'')]) + knobsC('LFO', [('speed',.5,''),('tension',.32,''),('skew',.5,''),('pw',.5,'')]) + '<div class="two">' + knobsC('TREMOLO') + knobsC('STRUM') + '</div></div></div>'
    return b

sans = '"Helvetica Neue",Helvetica,sans-serif'
pages = [
 page('option-A-paper', cssA, bodyA(), "{bg:'#f4f2ec',fg:'#16161a',hot:'#c8321e',grid:'#b9b5a8',font:'Helvetica Neue',vertical:false}"),
 page('option-B-terminal', cssB, bodyB(), "{bg:'#0c0d0e',fg:'#b7e36a',hot:'#ffffff',grid:'#3a4042',font:'Menlo',vertical:false}"),
 page('option-C-panel', cssC, bodyC(), "{bg:'#111111',fg:'#f3f3f1',hot:'#ff5a1f',grid:'#555555',font:'Helvetica Neue',vertical:false}"),
]

def dark(css):
    m = {'#f4f2ec':'@BG','#16161a':'@FG','#cfccc2':'#2b2b2e','#d8d5cb':'#333337','#555':'#8d8d90','#444':'#a5a5a8','#666':'#8d8d90','#888':'#6e6e72'}
    for k,v in m.items(): css = css.replace(k,v)
    return css.replace('@BG','#121214').replace('@FG','#e9e7e1')
pages.append(page('option-A-dark', dark(cssA), bodyA(), "{bg:'#121214',fg:'#e9e7e1',hot:'#ff6a4d',grid:'#3a3a3e',font:'Helvetica Neue',vertical:false}"))
chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
for p in pages:
    png = p[:-5] + '.png'
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=2', '--window-size=1440,900',
                    '--virtual-time-budget=4000', f'--screenshot={png}', 'file://' + p], stderr=subprocess.DEVNULL, check=False)
    print(png, os.path.exists(png))
