#!/usr/bin/env python3
# Génère Source/FontData.cpp à partir de Resources/fonts/*.ttf (polices libres OFL embarquées dans le plugin)
import os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fonts = [("michroma", "Michroma-Regular.ttf"), ("sairaMedium", "SairaSemiCondensed-Medium.ttf"),
         ("sairaBold", "SairaSemiCondensed-Bold.ttf"), ("shareTechMono", "ShareTechMono-Regular.ttf")]
out = ['// Fichier généré par tools/gen_fonts.py — ne pas modifier à la main', '#include "FontData.h"', 'namespace fontdata {']
for name, fn in fonts:
    data = open(os.path.join(root, "Resources", "fonts", fn), "rb").read()
    out.append("const unsigned char %s[] = {" % name)
    for i in range(0, len(data), 24):
        out.append(",".join(str(b) for b in data[i:i+24]) + ",")
    out.append("};")
    out.append("const int %sSize = %d;" % (name, len(data)))
out.append("}")
open(os.path.join(root, "Source", "FontData.cpp"), "w").write("\n".join(out) + "\n")
