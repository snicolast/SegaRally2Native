Gamepad button icons from 'Controller Input Icons' by ElDuderino, CC0 (public domain): https://opengameart.org/content/controller-input-icons.

Loaded by the menus' key help (src/ui/frontend.cpp, Frontend::pad_icon); a missing file falls back to the drawn button.

ps/: the same buttons as on a PlayStation pad (cross, circle, square, triangle, L1...), shown when
OPTIONS > CONTROLS > BUTTON PROMPTS is PLAYSTATION, or AUTO with a DualShock / DualSense. Drawn for
this port from the shapes above by ps/src/make.py (no Sony artwork). Frontend::pad_icon maps each
Xbox name to its file there (A: cross, LB: l1, MENU: options...).
