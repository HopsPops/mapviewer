# MapViewer

Simple gui program for viewing and downloading maps from internet.

## Batch mode

When passing --nogui argument you can download map in batch mode. For example:
```
mapvierwer.exe --nogui --longitude=5.0 --latitude=15.0 --zoom=3 --width=500 --height=300 --url="https
://a.tile.opentopomap.org/{zoom}/{x}/{y}.png" --output="image.jpg"
```

## Requirements
Requires curl in PATH

## Dependencies
GTK3

## Libraries
```
gtk-3
gdk-3
gdi32
imm32
shell32
ole32
uuid
winmm
dwmapi
setupapi
cfgmgr32
z
pangowin32-1.0
pangocairo-1.0
pango-1.0
atk-1.0
cairo-gobject
cairo
gdk_pixbuf-2.0
gio-2.0
gobject-2.0
glib-2.0
intl
```
