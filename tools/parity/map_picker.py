"""Searchable terminal map picker shared by parity launchers."""

import curses


def label(row):
    prefix = (row['edition'] + '-') if row.get('edition') else ''
    return f"{prefix}{row['slug']:<22}  {row.get('title', '') + ': ' if row.get('title') else ''}{row['name']}"


def filter_maps(rows, query):
    words = query.casefold().split()
    return [row for row in rows if all(word in ' '.join(str(value) for value in row.values()).casefold()
                                        for word in words)]


def picker_theme():
    theme = dict(text=curses.A_NORMAL, muted=curses.A_DIM, accent=curses.A_BOLD,
                 roc=curses.A_BOLD, tft=curses.A_BOLD, sc2=curses.A_BOLD,
                 selected=curses.A_REVERSE | curses.A_BOLD)
    if not curses.has_colors():
        return theme
    curses.start_color()
    curses.use_default_colors()
    palette = [('text', 252, curses.COLOR_WHITE, -1), ('muted', 244, curses.COLOR_WHITE, -1),
               ('accent', 222, curses.COLOR_YELLOW, -1), ('roc', 117, curses.COLOR_CYAN, -1),
               ('tft', 183, curses.COLOR_MAGENTA, -1), ('selected', 234, curses.COLOR_BLACK, 222)]
    for pair, (name, rich, basic, background) in enumerate(palette, 1):
        foreground = rich if curses.COLORS >= 256 else basic
        if background >= 0 and curses.COLORS < 256:
            background = curses.COLOR_YELLOW
        curses.init_pair(pair, foreground, background)
        theme[name] = curses.color_pair(pair) | (curses.A_BOLD if name in ('accent', 'selected') else 0)
    theme['sc2'] = theme['roc']
    return theme


def draw_picker(screen, choices, query, selected, total, theme,
                heading='W A R C R A F T   I I I', subtitle='CAMPAIGN ATLAS',
                compact_heading='WARCRAFT III'):
    height, width = screen.getmaxyx()
    screen.erase()

    def put(y, x, value, style='text', limit=None):
        available = min(width - x - 1, limit if limit is not None else width)
        if 0 <= y < height and x >= 0 and available > 0:
            try:
                screen.addnstr(y, x, value, available, theme[style])
            except curses.error:
                pass

    if height < 13 or width < 48:
        put(0, 0, compact_heading + '  /  Select a map', 'accent')
        put(1, 0, '> ' + query, 'accent')
        count, top = max(1, height - 3), 2
        start = max(0, selected - count + 1)
        for i, row in enumerate(choices[start:start + count]):
            put(top + i, 0, label(row), 'selected' if start + i == selected else 'text')
        put(height - 1, 0, 'Enter launch  /  Esc back', 'muted')
        screen.refresh()
        return count

    margin = 3 if width >= 80 else 1
    edge = width - margin - 1
    rule = '─' * (edge - margin)
    put(1, margin, heading, 'accent')
    put(2, margin, subtitle, 'muted')
    summary = f'{len(choices)} / {total} maps'
    put(2, max(margin + 20, edge - len(summary)), summary, 'muted')
    put(4, margin, '⌕  ' + (query if query else 'Search maps, chapters, campaigns…'),
        'accent' if query else 'muted')
    put(5, margin, rule, 'muted')
    alias_x, name_x = margin + 8, margin + (34 if width >= 90 else 24)
    put(6, margin + 2, 'GAME', 'muted')
    put(6, alias_x, 'MAP', 'muted')
    put(6, name_x, 'CHAPTER / DESTINATION', 'muted')
    count, top = max(1, height - 13), 7
    start = max(0, selected - count + 1)
    for i, row in enumerate(choices[start:start + count]):
        active = start + i == selected
        y = top + i
        if active:
            put(y, margin, ' ' * (edge - margin), 'selected')
        put(y, margin, '›' if active else ' ', 'selected' if active else 'text')
        category_style = row['edition'] if row['edition'] in theme else 'accent'
        put(y, margin + 2, row['edition'].upper(), 'selected' if active else category_style)
        alias = row['slug']
        space = name_x - alias_x - 2
        put(y, alias_x, alias if len(alias) <= space else alias[:space - 1] + '…',
            'selected' if active else 'muted', space)
        title = row.get('title', '') + ' · ' if row.get('title') else ''
        put(y, name_x, title + row['name'], 'selected' if active else 'text', edge - name_x)
    if not choices:
        put(top + 1, margin + 2, 'No maps match your search.', 'accent')
        put(top + 2, margin + 2, 'Try a name or chapter, or press Ctrl-U to clear.', 'muted')
    put(height - 5, margin, rule, 'muted')
    if choices:
        row = choices[selected]
        put(height - 4, margin, row['name'], 'accent')
        put(height - 3, margin, row.get('campaign') or 'Return to the main menu', 'muted')
        put(height - 2, margin, row['edition'] + '-' + row['slug'] +
            ('  ·  ' + row['path'] if row.get('path') else ''), 'muted')
        position = f'{selected + 1} / {len(choices)}'
        put(height - 5, edge - len(position) - 1, ' ' + position, 'muted')
    put(height - 1, margin, '↑↓ Choose   Enter Launch   Esc Back   Ctrl-U Clear   PgUp/PgDn Scroll', 'muted')
    screen.refresh()
    return count


def pick_map(screen, rows, heading='W A R C R A F T   I I I', subtitle='CAMPAIGN ATLAS',
             compact_heading='WARCRAFT III'):
    curses.curs_set(0)
    theme = picker_theme()
    query, selected = '', 0
    screen.keypad(True)
    while True:
        choices = filter_maps(rows, query)
        selected = max(0, min(selected, len(choices) - 1))
        count = draw_picker(screen, choices, query, selected, len(rows), theme,
                            heading, subtitle, compact_heading)
        key = screen.get_wch()
        if key in ('\x1b', '\x03'):
            return None
        if key in ('\n', '\r', curses.KEY_ENTER) and choices:
            return choices[selected]
        if key in (curses.KEY_DOWN, '\t'):
            selected = min(selected + 1, len(choices) - 1)
        elif key == curses.KEY_UP:
            selected = max(0, selected - 1)
        elif key == curses.KEY_NPAGE:
            selected += count
        elif key == curses.KEY_PPAGE:
            selected -= count
        elif key in (curses.KEY_BACKSPACE, '\x7f', '\b'):
            query, selected = query[:-1], 0
        elif key == '\x15':
            query, selected = '', 0
        elif isinstance(key, str) and key.isprintable():
            query, selected = query + key, 0
