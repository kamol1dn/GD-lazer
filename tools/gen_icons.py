import json, re
BS = chr(92)
icons = [
 ('MUSIC',0xf001),('STAR',0xf005),('USER',0xf007),('CHECK',0xf00c),('XMARK',0xf00d),('GEAR',0xf013),
 ('HOUSE',0xf015),('LOCK',0xf023),('VOLUME',0xf028),('PLAY',0xf04b),('CHEVRON_LEFT',0xf053),
 ('CHEVRON_RIGHT',0xf054),('CIRCLE_XMARK',0xf057),('CIRCLE_INFO',0xf05a),('GIFT',0xf06b),('EYE',0xf06e),
 ('CHART',0xf080),('KEY',0xf084),('GEARS',0xf085),('TROPHY',0xf091),('GLOBE',0xf0ac),('WRENCH',0xf0ad),
 ('USERS',0xf0c0),('BELL',0xf0f3),('GAMEPAD',0xf11b),('KEYBOARD',0xf11c),('PUZZLE',0xf12e),('SHIELD',0xf132),
 ('SLIDERS',0xf1de),('PEN',0xf304),('DESKTOP',0xf390),('GEM',0xf3a5),('BOX_OPEN',0xf49e),('COINS',0xf51e),
 ('SHIRT',0xf553),('MEDAL',0xf5a2),('SEARCH',0xf002),
]
icons.sort(key=lambda x: x[1])
lines = []
for name, cp in icons:
    esc = ''.join(BS + 'x%02X' % b for b in chr(cp).encode('utf-8'))
    lines.append(f'    constexpr auto {name} = "{esc}";'.ljust(52) + f'// {cp:04x}')
block = 'namespace icon {\n' + '\n'.join(lines) + '\n}'
p = 'src/ui/Text.hpp'
s = open(p, encoding='utf-8').read()
s = re.sub(r'namespace icon \{.*?\n\}', lambda m: block, s, flags=re.S)
s = s.replace('// Font Awesome Free glyphs baked into the icon font (codepoints listed in mod.json).',
              '// Font Awesome Free glyphs baked into the icon font. Generated together with the\n// "icons" charset in mod.json: add new glyphs to both.')
open(p, 'w', encoding='utf-8').write(s)
m = json.load(open('mod.json', encoding='utf-8'))
m['resources']['fonts']['icons']['charset'] = ','.join(str(cp) for _, cp in icons)
with open('mod.json', 'w', encoding='utf-8') as f:
    json.dump(m, f, indent='\t', ensure_ascii=False)
    f.write('\n')
