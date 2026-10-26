"""Check the installed system launcher without a user-local desktop entry."""
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gio, Gtk

app = Gio.DesktopAppInfo.new("asm101.desktop")
assert app is not None, "Installed launcher is missing"
assert app.get_filename() == "/usr/share/applications/asm101.desktop", app.get_filename()
assert app.should_show(), "Launcher is hidden from the application menu"
assert app.get_executable() == "/usr/bin/asm101", app.get_executable()
assert any(app.get_id() == "asm101.desktop" for app in Gio.AppInfo.get_all())
assert any("asm101.desktop" in group for group in Gio.DesktopAppInfo.search("ASM101"))
assert Gtk.IconTheme.get_default().lookup_icon("asm101", 64, 0) is not None, "Installed icon cannot be found"
print("PASS: system launcher visible, searchable, executable resolved, icon found")
