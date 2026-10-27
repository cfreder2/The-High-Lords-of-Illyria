# Draws rivers, roads, crossings, cities, sites and names over the continent
# render:  python3 label.py map.ppm map.json continent-map.png
import json, sys
from PIL import Image, ImageDraw, ImageFont
img = Image.open(sys.argv[1]).convert('RGB')
data = json.load(open(sys.argv[2]))
W, H = img.size
scale = 2
big = img.resize((W*scale, H*scale), Image.BICUBIC)
d = ImageDraw.Draw(big)
def P(p): return (p[0]*scale, p[1]*scale)
for rv in data['rivers']:
    pts = [P(p) for p in rv['pts']]
    d.line(pts, fill=(40, 90, 165), width=max(3, int(rv['w']/4)), joint='curve')
for road in data['roads']:
    pts = [P(p) for p in road]
    for i in range(len(pts)-1):
        (x0,y0),(x1,y1) = pts[i], pts[i+1]
        n = int(((x1-x0)**2+(y1-y0)**2)**0.5 / 10) + 1
        for k in range(0, n, 2):
            a = (x0+(x1-x0)*k/n, y0+(y1-y0)*k/n); b = (x0+(x1-x0)*min(k+1,n)/n, y0+(y1-y0)*min(k+1,n)/n)
            d.line([a,b], fill=(110, 70, 35), width=3)
def font(sz, bold=False):
    for f in (['/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'] if bold else []) + ['/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf']:
        try: return ImageFont.truetype(f, sz)
        except Exception: pass
    return ImageFont.load_default()
for c in data['crossings']:
    x, y = P(c['p'])
    if c['k'] == 0: d.ellipse([x-9,y-9,x+9,y+9], outline=(250,250,255), width=3)
    else: d.rectangle([x-10,y-5,x+10,y+5], fill=(120,80,40), outline=(250,240,220), width=2)
FACTION = [(245, 245, 240), (230, 190, 40), (200, 40, 30), (230, 120, 30), (40, 150, 60), (150, 100, 60), (50, 90, 200), (25, 25, 25)]
small = font(13)
for c in data.get('sites', []):
    x, y = P(c['p'])
    if c['k'] == 0:  # ruin: broken pillars
        d.polygon([(x-7,y+6),(x-4,y-6),(x-1,y+6)], fill=(70,60,55)); d.polygon([(x+1,y+6),(x+4,y-2),(x+7,y+6)], fill=(70,60,55))
    elif c['k'] == 1:  # temple
        d.polygon([(x-9,y+7),(x+9,y+7),(x+9,y-2),(x,y-9),(x-9,y-2)], fill=(240,235,215), outline=(60,50,40))
    else:  # sage
        d.ellipse([x-8,y-8,x+8,y+8], fill=(120,60,170), outline=(250,240,255), width=2)
for c in data.get('cities', []):
    x, y = P(c['p'])
    col = FACTION[c['o']] if c['o'] >= 0 else (150, 150, 150)
    s_ = 15 if c['cap'] else 9
    d.rectangle([x-s_, y-s_, x+s_, y+s_], fill=col, outline=(30, 20, 10), width=3 if c['cap'] else 2)
    if c['cap']:
        for dx in (-s_, s_):
            for dy in (-s_, s_):
                d.rectangle([x+dx-5, y+dy-5, x+dx+5, y+dy+5], fill=col, outline=(30,20,10), width=2)
    f = font(17, True) if c['cap'] else small
    tw = d.textlength(c['n'], font=f)
    d.text((x - tw/2 + 1, y + s_ + 3), c['n'], font=f, fill=(250, 245, 230))
    d.text((x - tw/2, y + s_ + 2), c['n'], font=f, fill=(40, 25, 15))
styles = {0: (font(34, True), (90, 30, 20)), 1: (font(20), (60, 45, 35)), 2: (font(22), (25, 50, 110)), 3: (font(20), (20, 55, 25)), 4: (font(18), (255, 250, 235))}
for l in data['labels']:
    f, col = styles[l['k']]
    x, y = P(l['p'])
    tw = d.textlength(l['n'], font=f)
    if l['k'] == 4: y += 14
    d.text((x - tw/2 + 1, y + 1), l['n'], font=f, fill=(245, 235, 210) if l['k'] != 4 else (20, 20, 20))
    d.text((x - tw/2, y), l['n'], font=f, fill=col)
# parchment border
border = 26
out = Image.new('RGB', (big.width + border*2, big.height + border*2 + 60), (196, 168, 120))
out.paste(big, (border, border + 60))
od = ImageDraw.Draw(out)
od.rectangle([border-6, border+54, border+big.width+5, border+60+big.height+5], outline=(150, 40, 30), width=6)
t = 'THE CONTINENT OF ILLYRIA'
tf = font(40, True)
od.text(((out.width - od.textlength(t, font=tf))/2, 18), t, font=tf, fill=(110, 30, 20))
od.text((border, out.height - 22), '12.8 km x 16 km  ·  castles = capitals, squares = cities, pillars = ruins, roofs = temples, violet = sages  ·  original map after Warlords (1990)', font=font(16), fill=(80, 50, 30))
out.resize((out.width//2, out.height//2), Image.LANCZOS).save(sys.argv[3])
print(out.size)
