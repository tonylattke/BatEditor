#!/usr/bin/env python3
"""Regenera src/I18nTable.cpp a partir de los textos de la interfaz.

Uso (desde la carpeta del proyecto):   python tools/i18n/update_translations.py

1. Busca en src/*.cpp los textos traducibles: tr("..."), QObject::tr("..."),
   QCoreApplication::translate("contexto", "...") y gtr("...").
2. Les asigna su traducción al inglés desde translations_en.py.
3. Escribe src/I18nTable.cpp e indica los textos que aún no tienen traducción
   (añádelos a translations_en.py y vuelve a ejecutar el script).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from translations_en import EN, SHORTCUT_PHRASES, QT_ES  # noqa: E402

LITERAL = re.compile(r'\s*(?:R"\((.*?)\)"|"((?:[^"\\]|\\.)*)")', re.S)
CALL = re.compile(r'(\bQObject::tr|\btr|\bgtr)\(\s*|QCoreApplication::translate\(\s*"[^"]*"\s*,\s*')
ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', 'r': '\r', "'": "'"}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            out.append(ESCAPES.get(s[i + 1], '\\' + s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def extract(src_dir):
    found = {}
    for name in sorted(os.listdir(src_dir)):
        if not name.endswith('.cpp') or name == 'I18nTable.cpp':
            continue
        text = open(os.path.join(src_dir, name), encoding='utf-8').read()
        for m in CALL.finditer(text):
            pos, parts = m.end(), []
            while True:
                lm = LITERAL.match(text, pos)
                if not lm:
                    break
                parts.append(lm.group(1) if lm.group(1) is not None else unescape(lm.group(2)))
                pos = lm.end()
            if parts:
                found.setdefault(''.join(parts), name)
    return found


def c_literal(s):
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
    return '"' + s + '"'


def main():
    src = os.path.join(ROOT, 'src')
    strings = extract(src)
    pairs, missing = [], []
    for es, where in strings.items():
        if es in EN:
            pairs.append((es, EN[es]))
        elif 'Atajos de BatCode' in es:
            en = es
            for a, b in SHORTCUT_PHRASES:
                en = en.replace(a, b)
            pairs.append((es, en))
        else:
            missing.append((es, where))

    lines = [
        '// GENERADO por tools/i18n/update_translations.py: no editar a mano.',
        '// Textos de la interfaz: español (original) -> inglés. Codificación UTF-8 (MSVC: /utf-8).',
        '',
        'extern const char *const kSpanishToEnglish[][2] = {',
    ]
    for es, en in pairs:
        lines.append('    {%s,\n     %s},' % (c_literal(es), c_literal(en)))
    lines += [
        '};',
        'extern const int kSpanishToEnglishCount = int(sizeof(kSpanishToEnglish) / sizeof(kSpanishToEnglish[0]));',
        '',
        '// Textos propios de Qt (botones de diálogos, menú contextual de edición) en el modo español',
        'extern const char *const kQtToSpanish[][2] = {',
    ]
    for en, es in QT_ES.items():
        lines.append('    {%s, %s},' % (c_literal(en), c_literal(es)))
    lines += ['};', 'extern const int kQtToSpanishCount = int(sizeof(kQtToSpanish) / sizeof(kQtToSpanish[0]));', '']
    with open(os.path.join(src, 'I18nTable.cpp'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))

    print('Textos traducidos: %d' % len(pairs))
    if missing:
        print('\nSIN TRADUCCIÓN (%d) — añádelos a tools/i18n/translations_en.py:' % len(missing))
        for es, where in missing:
            print('  [%s] %r' % (where, es))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
