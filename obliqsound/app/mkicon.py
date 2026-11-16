# The house icon generator moved to the top of the faltung bundle: scripts/mkicon.py
#   python3 ../../scripts/mkicon.py "ObliqSound" app/icon.png
import os, runpy, sys
sys.argv[0] = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'scripts', 'mkicon.py')
runpy.run_path(sys.argv[0], run_name='__main__')
