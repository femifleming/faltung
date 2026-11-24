# putz icon: Septambres Neau "Z", faltung mustard on ms0 slate (house style, see obliqsound/app/mkicon.py)
import sys, importlib.util, subprocess, os
spec = importlib.util.spec_from_file_location("mk", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "obliqsound", "app", "mkicon.py"))
mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk)
mk.ICONS["putz"] = ("Z", "#c9b53b", "#1a242c")
os.makedirs("build/AppIcon.iconset", exist_ok=True)
for s in [16, 32, 64, 128, 256, 512, 1024]:
    img = mk.icon("putz", s)
    if s <= 512: img.save(f"build/AppIcon.iconset/icon_{s}x{s}.png")
    if s >= 32: img.save(f"build/AppIcon.iconset/icon_{s//2}x{s//2}@2x.png")
subprocess.run(["iconutil", "-c", "icns", "build/AppIcon.iconset", "-o", "Resources/AppIcon.icns"], check=True)
mk.icon("putz", 512).save("Resources/icon.png")
