import sys
from PIL import Image, ImageDraw
names = sys.argv[2:]
out = sys.argv[1]
cols = 3
rows = (len(names)+cols-1)//cols
W = 240
sheet = Image.new("RGB", (cols*(W+10)+10, rows*(W+30)+10), (30,30,30))
d = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    im = Image.open("out/%s.ppm" % n)
    x = 10 + (i % cols)*(W+10); y = 10 + (i//cols)*(W+30)
    sheet.paste(im, (x, y+16))
    d.text((x, y), n, fill=(220,220,220))
sheet.save(out)
