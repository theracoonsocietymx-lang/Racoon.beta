# Configuracion de dmgbuild (ventana del instalador). La llama make_dmg.sh.
import os
app = defines["app"]
bg = defines.get("bg", "")
appname = os.path.basename(app)
files = [app]
symlinks = {"Aplicaciones": "/Applications"}
icon_locations = {appname: (170, 205), "Aplicaciones": (490, 205)}
if bg and os.path.exists(bg):
    background = bg
window_rect = ((200, 140), (660, 420))
default_view = "icon-view"
icon_size = 128
text_size = 13
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
include_icon_view_settings = True
format = "UDZO"
filesystem = "HFS+"
hide_extensions = [appname]
