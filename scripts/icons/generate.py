"""Generate Superbird's app icon SVGs.

Usage: generate.py <Ladybird's org.ladybird.Ladybird.svg> <output dir>

The tile (squircle, rim and drop shadow) is taken from Ladybird's flat icon so both apps sit in the Dock at the same size;
only its colours change. The mark is the "hero wings" bird with a star on its chest. See make.sh, which renders the PNGs.
"""
import re, math, os, sys
SRC=sys.argv[1]; OUT=sys.argv[2]
lb=open(SRC).read()
paths=re.findall(r'<path ([^>]*)/>',lb)
tile=[re.search(r'd="([^"]*)"',p).group(1) for p in paths[:4]]
shadow=re.search(r'(<g fill="#1c1238".*?</g>)',lb,re.S).group(1)

# Hero wings (D1), left half from top centre round the wing to the tail tip; mirrored.
L=[(512,440),(262,268),(328,360),(234,348),(318,438),(240,446),(452,548),(470,712),(512,662)]
def bake(s, cx=512, cy=512, oy=490):
    return lambda p:(cx+s*(p[0]-512), cy+s*(p[1]-oy))
def bird(T):
    pts=L+[(1024-x,y) for x,y in reversed(L[1:-1])]
    return "M"+"L".join(f"{T(p)[0]:.2f} {T(p)[1]:.2f}" for p in pts)+"Z"
def star(T,cx=512,cy=512,r=58):
    out=[]
    for i in range(10):
        a=-math.pi/2+i*math.pi/5; rr=r*0.45 if i%2 else r
        out.append(T((cx+rr*math.cos(a), cy+rr*math.sin(a))))
    return "M"+"L".join(f"{x:.2f} {y:.2f}" for x,y in out)+"Z"

FLAT=1.08                 # mark scale on the 824px tile
FULL=FLAT*1024/824        # same proportion on Icon Composer's full-bleed canvas
Tf=bake(FLAT); Tc=bake(FULL)

def flat(bg0,bg1,rim0,rim1,edge,under,bird_c,star_c):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>
    <linearGradient id="rim" gradientUnits="userSpaceOnUse" x1="0" y1="100" x2="0" y2="914">
      <stop offset="0" stop-color="{rim0}"/>
      <stop offset="1" stop-color="{rim1}"/>
    </linearGradient>
    <linearGradient id="fill" gradientUnits="userSpaceOnUse" x1="0" y1="110" x2="0" y2="904">
      <stop offset="0" stop-color="{bg0}"/>
      <stop offset="1" stop-color="{bg1}"/>
    </linearGradient>
  </defs>
  {shadow}
  <path fill="{under}" d="{tile[0]}"/>
  <path fill="url(#rim)" d="{tile[1]}"/>
  <path fill="{edge}" d="{tile[2]}"/>
  <path fill="url(#fill)" d="{tile[3]}"/>
  <path fill="{bird_c}" d="{bird(Tf)}"/>
  <path fill="{star_c}" d="{star(Tf)}"/>
</svg>
'''
def mark(bird_c,star_c):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<path fill="{bird_c}" d="{bird(Tc)}"/>
<path fill="{star_c}" d="{star(Tc)}"/>
</svg>
'''
def mark_mono():
    # Tinted icons are one colour, so the star is cut out of the bird instead.
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<path fill="#ffffff" fill-rule="evenodd" d="{bird(Tc)}{star(Tc)}"/>
</svg>
'''
os.makedirs(OUT+"/AppIcon.icon/Assets",exist_ok=True)
open(OUT+"/superbird.svg","w").write(flat("#252836","#121320","#1a1c27","#0d0e17","#2f3242","#0a0b12","#ffffff","#ff3b4e"))
# Private windows: a violet night, so they stand apart from regular windows in the taskbar.
open(OUT+"/superbird-private.svg","w").write(flat("#2b1f52","#160f30","#1e1640","#0e0a22","#3a2c68","#0b0819","#d9ccff","#ff6b7c"))
open(OUT+"/AppIcon.icon/Assets/mark.svg","w").write(mark("#ffffff","#ff3b4e"))
open(OUT+"/AppIcon.icon/Assets/mark-dark.svg","w").write(mark("#ffffff","#ff4d5e"))
open(OUT+"/AppIcon.icon/Assets/mark-mono.svg","w").write(mark_mono())

# Icon Composer's description: Ladybird's, with Superbird's midnight background.
import json
def srgb(h):
    return "srgb:" + ",".join(f"{int(h[i:i + 2], 16) / 255:.5f}" for i in (1, 3, 5)) + ",1.00000"
icon = json.load(open(os.path.join(os.path.dirname(SRC), "../../../UI/Icons/macos/AppIcon.icon/icon.json")))
icon["fill"] = {"linear-gradient": [srgb("#252836"), srgb("#121320")]}
icon["fill-specializations"][0]["value"] = {"linear-gradient": [srgb("#1e2030"), srgb("#0c0d16")]}
open(OUT + "/AppIcon.icon/icon.json", "w").write(json.dumps(icon, indent=2, separators=(",", " : ")) + "\n")
