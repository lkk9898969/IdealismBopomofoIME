"""Splits McBopomofo's data.txt into phrase packs (詞庫包) that can be turned on and off.

    py -3 tools/split_packs.py <data.txt> <Symbols.txt> <out dir>

Writes base.txt, phrases.txt, symbols.txt and the manifest packs.txt into <out dir>. Every pack
keeps McBopomofo's sorted format (header line, rows sorted by key bytes) so the IME can
memory-map it and binary-search it. Splitting a sorted file keeps each part sorted.

  base     基礎字庫   single characters + the engine's internal readings (punctuation, …).
                     Required: without it nothing can be typed.
  phrases  一般詞彙   words of two or more syllables.
  symbols  符號與表情 emoji, flags and symbols offered for ordinary readings (Symbols.txt).

MACRO@ rows are dropped: McBopomofo expands them into dates at run time, which this IME does not
do, so they would show up as literal "MACRO@DATE_…" candidates.
"""
import os
import sys

HEADER = '# format org.openvanilla.mcbopomofo.sorted\n'

PACKS = [
    # id, file, required, name, description
    ('base', 'base.txt', 1, '基礎字庫', '單字與標點符號（必要，不能停用）'),
    ('phrases', 'phrases.txt', 0, '一般詞彙', '兩個字以上的詞，整句選字靠它'),
    ('symbols', 'symbols.txt', 0, '符號與表情', '打注音時出現的表情符號、國旗、特殊符號（例如 ㄅㄚ → ⑧）'),
]


def main():
    data_path, symbols_path, out_dir = sys.argv[1:4]
    symbols = set()
    with open(symbols_path, encoding='utf-8') as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                symbols.add((parts[1], parts[0]))  # (reading, value)

    rows = {pack[0]: [] for pack in PACKS}
    dropped = 0
    with open(data_path, encoding='utf-8', newline='') as f:
        header = f.readline()
        if header != HEADER:
            sys.exit('unexpected header in %s: %r' % (data_path, header))
        for line in f:
            parts = line.rstrip('\n').split(' ')
            if len(parts) != 3:
                sys.exit('malformed row: %r' % line)
            reading, value, _ = parts
            if value.startswith('MACRO@'):
                dropped += 1
            elif reading.startswith('_'):
                rows['base'].append(line)
            elif (reading, value) in symbols:
                rows['symbols'].append(line)
            elif '-' in reading:
                rows['phrases'].append(line)
            else:
                rows['base'].append(line)

    os.makedirs(out_dir, exist_ok=True)
    manifest = ['# id\tfile\trequired\tname\tdescription\trows\n']
    for pack_id, file_name, required, name, description in PACKS:
        lines = rows[pack_id]
        with open(os.path.join(out_dir, file_name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(HEADER)
            f.writelines(lines)
        manifest.append('%s\t%s\t%d\t%s\t%s\t%d\n' % (pack_id, file_name, required, name, description, len(lines)))
        print('%-8s %7d rows' % (pack_id, len(lines)))
    with open(os.path.join(out_dir, 'packs.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.writelines(manifest)
    print('dropped %d MACRO@ rows' % dropped)


if __name__ == '__main__':
    main()
