#!/usr/bin/env python3
"""Collects every non-ASCII character in the games' own achievement words —
all the locales their SPAs carry — into thirdparty/notocjk/game_chars.txt,
so tools/gen_cjk_font.py can put those glyphs in the compiled-in font.

    tools/gen_game_chars.py ~/GithubRepo/Dead_Rising_2_Case_Zero_Xenon_Recomp/assets/game \\
                            ~/GithubRepo/Dead_Rising_2_Case_West_Xenon_Recomp/assets/game

Needs XenonLive's tools beside this repo (or XLIVE_ROOT) for the SPA reader.
Run it when a game is added, then gen_cjk_font.py.
"""
import os, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
xlive = pathlib.Path(os.environ.get('XLIVE_ROOT', root.parent / 'XenonLive'))
sys.path.insert(0, str(xlive / 'tools'))
import spa_import, gameconfig  # noqa: E402

chars = set()
for arg in sys.argv[1:]:
    spa, found_in = spa_import.locate(pathlib.Path(arg))
    _, xml = spa.source()
    cfg = gameconfig.parse(xml)
    for locale in cfg.locales:
        c = gameconfig.parse(xml, locale)
        for a in c.achievements:
            for text in (a.name, a.locked_description, a.unlocked_description):
                chars.update(ch for ch in text if ord(ch) > 0x7F)
        chars.update(ch for ch in (c.title_name or "") if ord(ch) > 0x7F)
    print(f'{found_in.name}: {", ".join(cfg.locales)}')
out = root / 'thirdparty' / 'notocjk' / 'game_chars.txt'
out.write_text(''.join(sorted(chars)) + '\n', encoding='utf-8')
print(f'{len(chars)} characters -> {out.relative_to(root)}')
