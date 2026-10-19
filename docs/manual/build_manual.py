#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the Lumora user guide PDF from docs/manual/manual.md.

    python3 docs/manual/build_manual.py            # writes docs/Lumora-Manual.pdf

Needs reportlab (pip install reportlab). The version printed on the guide is read from
src/ui/main.cpp (setApplicationVersion), and written to docs/manual/built-for.txt so the
test suite can tell when the guide is older than the program.

The source is a small markdown subset:
  # Part                      ## Chapter {#anchor}        ### Section {#optional-anchor}
  paragraphs, "- " bullets, "1. " numbered items, ``` code fences, | pipe | tables |,
  ![caption](images/file.png), > **Tip:** / > **Note:** callouts,
  [text](#anchor) links, **bold**, *italic*, `code`.
"""
import os
import re
import sys
import datetime

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, Image, KeepTogether, NextPageTemplate,
                                PageBreak, PageTemplate, Paragraph, Preformatted, Spacer, Table,
                                TableStyle, CondPageBreak)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.lib.fonts import addMapping

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SOURCE = os.path.join(HERE, 'manual.md')
OUTPUT = os.path.join(ROOT, 'docs', 'Lumora-Manual.pdf')
STAMP = os.path.join(HERE, 'built-for.txt')

ACCENT = colors.HexColor('#E0A030')
DARK = colors.HexColor('#1E2228')
INK = colors.HexColor('#22262C')
MUTED = colors.HexColor('#6A707A')
LINK = colors.HexColor('#1F6FB8')
CODE_BG = colors.HexColor('#F3F4F6')
RULE = colors.HexColor('#D5D8DD')
TIP_BG = colors.HexColor('#FFF6E3')
NOTE_BG = colors.HexColor('#E9F2FB')


def app_version():
    with open(os.path.join(ROOT, 'src', 'ui', 'main.cpp'), encoding='utf-8') as f:
        m = re.search(r'setApplicationVersion\(\s*(?:QStringLiteral\()?"([^"]+)"', f.read())
    if not m:
        sys.exit('could not find setApplicationVersion in src/ui/main.cpp')
    return m.group(1)


def register_fonts():
    fonts = os.path.join(HERE, 'fonts')
    for name, file in (('Sans', 'DejaVuSans.ttf'), ('Sans-Bold', 'DejaVuSans-Bold.ttf'),
                       ('Sans-Italic', 'DejaVuSans-Oblique.ttf'),
                       ('Sans-BoldItalic', 'DejaVuSans-BoldOblique.ttf'), ('Mono', 'DejaVuSansMono.ttf')):
        pdfmetrics.registerFont(TTFont(name, os.path.join(fonts, file)))
    addMapping('Sans', 0, 0, 'Sans')
    addMapping('Sans', 1, 0, 'Sans-Bold')
    addMapping('Sans', 0, 1, 'Sans-Italic')
    addMapping('Sans', 1, 1, 'Sans-BoldItalic')
    addMapping('Mono', 0, 0, 'Mono')
    addMapping('Mono', 1, 0, 'Mono')
    addMapping('Mono', 0, 1, 'Mono')
    addMapping('Mono', 1, 1, 'Mono')


def styles():
    s = {}
    base = ParagraphStyle('body', fontName='Sans', fontSize=10, leading=14.5, textColor=INK, spaceAfter=7)
    s['body'] = base
    s['bullet'] = ParagraphStyle('bullet', parent=base, leftIndent=16, bulletIndent=4, spaceAfter=4)
    s['number'] = ParagraphStyle('number', parent=base, leftIndent=20, bulletIndent=2, spaceAfter=4)
    s['h1'] = ParagraphStyle('h1', parent=base, fontName='Sans-Bold', fontSize=22, leading=27,
                             textColor=DARK, spaceBefore=0, spaceAfter=6)
    s['h2'] = ParagraphStyle('h2', parent=base, fontName='Sans-Bold', fontSize=13.5, leading=18,
                             textColor=DARK, spaceBefore=12, spaceAfter=5)
    s['h3'] = ParagraphStyle('h3', parent=base, fontName='Sans-Bold', fontSize=11, leading=15,
                             textColor=DARK, spaceBefore=8, spaceAfter=4)
    s['chapnum'] = ParagraphStyle('chapnum', parent=base, fontName='Sans-Bold', fontSize=10,
                                  textColor=ACCENT, spaceAfter=2)
    s['code'] = ParagraphStyle('code', fontName='Mono', fontSize=8.6, leading=12, textColor=INK)
    s['cell'] = ParagraphStyle('cell', parent=base, fontSize=9, leading=12.5, spaceAfter=0)
    s['cellhead'] = ParagraphStyle('cellhead', parent=s['cell'], fontName='Sans-Bold', textColor=colors.white)
    s['caption'] = ParagraphStyle('caption', parent=base, fontName='Sans-Italic', fontSize=8.8,
                                  textColor=MUTED, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
    s['callout'] = ParagraphStyle('callout', parent=base, spaceAfter=0)
    s['inchapter'] = ParagraphStyle('inchapter', parent=base, fontSize=9, leading=13, textColor=MUTED,
                                    spaceAfter=10)
    s['toc0'] = ParagraphStyle('toc0', fontName='Sans-Bold', fontSize=11.5, leading=15, textColor=DARK,
                               spaceBefore=10, leftIndent=0)
    s['toc1'] = ParagraphStyle('toc1', fontName='Sans', fontSize=10, leading=14, textColor=INK,
                               leftIndent=14, firstLineIndent=0)
    s['parttitle'] = ParagraphStyle('parttitle', parent=base, fontName='Sans-Bold', fontSize=30, leading=36,
                                    textColor=DARK, spaceAfter=18)
    s['partnum'] = ParagraphStyle('partnum', parent=base, fontName='Sans-Bold', fontSize=13,
                                  textColor=ACCENT, spaceAfter=6)
    s['partlink'] = ParagraphStyle('partlink', parent=base, fontSize=11.5, leading=19, leftIndent=4)
    return s


# ---------------------------------------------------------------- inline markup

def slug(text):
    return re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')


def esc(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def inline(text):
    """Converts the inline markdown subset into reportlab paragraph markup."""
    codes = []

    def keep_code(m):
        codes.append('<font name="Mono" backColor="#EEF0F3">&#160;%s&#160;</font>' % esc(m.group(1)))
        return '\x00%d\x00' % (len(codes) - 1)

    text = re.sub(r'`([^`]+)`', keep_code, text)
    links = []

    def keep_link(m):
        label, target = m.group(1), m.group(2)
        if target.startswith('#'):
            links.append(('<a href="%s" color="#1F6FB8">' % target, '</a>'))
        else:
            links.append(('<a href="%s" color="#1F6FB8"><u>' % esc(target), '</u></a>'))
        return '\x01%d\x01%s\x02' % (len(links) - 1, label)

    text = re.sub(r'\[([^\]]+)\]\(([^)\s]+)\)', keep_link, text)
    text = esc(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<i>\1</i>', text)
    text = re.sub(r'\x01(\d+)\x01', lambda m: links[int(m.group(1))][0], text)
    # Close each link in the order they were opened.
    order = iter(re.findall(r'<a href="[^"]*" color="#1F6FB8">(<u>)?', text))
    text = re.sub('\x02', lambda m: '</u></a>' if next(order, None) else '</a>', text)
    text = re.sub(r'\x00(\d+)\x00', lambda m: codes[int(m.group(1))], text)
    return text


# ---------------------------------------------------------------- flowables that know about the outline

class Heading(Paragraph):
    """A heading that becomes a PDF bookmark, an outline entry and (levels 0-1) a contents line."""

    def __init__(self, text, style, level, key, toc_text=None, outline_text=None):
        super().__init__('<a name="%s"/>%s' % (key, text), style)
        self.level, self.key = level, key
        self.toc_text = toc_text
        self.outline_text = outline_text


class PartHeading(Heading):
    pass


class ManualDoc(BaseDocTemplate):
    def __init__(self, filename, version, **kw):
        super().__init__(filename, pagesize=letter, leftMargin=0.9 * inch, rightMargin=0.9 * inch,
                         topMargin=0.95 * inch, bottomMargin=0.85 * inch,
                         title='Lumora User Guide', author='Lumora',
                         subject='Lumora %s user guide and reference' % version, **kw)
        self.version = version
        self.chapter = ''
        w, h = self.pagesize
        frame = Frame(self.leftMargin, self.bottomMargin, w - self.leftMargin - self.rightMargin,
                      h - self.topMargin - self.bottomMargin, id='body', leftPadding=0, rightPadding=0,
                      topPadding=0, bottomPadding=0)
        self.addPageTemplates([
            PageTemplate('cover', [frame], onPage=self.cover_page),
            PageTemplate('front', [frame], onPageEnd=self.front_page),
            PageTemplate('body', [frame], onPageEnd=self.body_page),
        ])

    def afterFlowable(self, flowable):
        if not isinstance(flowable, Heading):
            return
        title = flowable.outline_text or flowable.getPlainText()
        if flowable.level == 1:
            self.chapter = title
        if isinstance(flowable, PartHeading):
            self.chapter = ''
        self.canv.addOutlineEntry(title, flowable.key, level=flowable.level, closed=flowable.level >= 1)
        if flowable.toc_text is not None:
            self.notify('TOCEntry', (flowable.level, flowable.toc_text, self.page, flowable.key))

    # -- page decorations
    def cover_page(self, canv, doc):
        w, h = doc.pagesize
        canv.saveState()
        canv.setFillColor(DARK)
        canv.rect(0, 0, w, h, stroke=0, fill=1)
        canv.setFillColor(ACCENT)
        canv.rect(0, h * 0.42, w, 6, stroke=0, fill=1)
        icon = os.path.join(ROOT, 'resources', 'lumora.png')
        if os.path.exists(icon):
            canv.drawImage(icon, 0.9 * inch, h * 0.55 + 70, width=96, height=96, mask='auto')
        canv.setFillColor(colors.white)
        canv.setFont('Sans-Bold', 54)
        canv.drawString(0.9 * inch, h * 0.55, 'Lumora')
        canv.setFont('Sans', 22)
        canv.drawString(0.9 * inch, h * 0.55 - 38, 'User guide and reference')
        canv.setFillColor(ACCENT)
        canv.setFont('Sans-Bold', 13)
        canv.drawString(0.9 * inch, h * 0.42 - 34, 'Version %s' % doc.version)
        canv.setFillColor(colors.HexColor('#AEB4BD'))
        canv.setFont('Sans', 10.5)
        canv.drawString(0.9 * inch, h * 0.42 - 54, 'From your first cue to every advanced feature.')
        canv.drawString(0.9 * inch, 0.9 * inch, 'Built %s  ·  Free software under the GNU GPL v3 or later'
                        % datetime.date.today().isoformat())
        canv.restoreState()

    def _footer(self, canv, doc, roman=False):
        w, h = doc.pagesize
        canv.saveState()
        canv.setStrokeColor(RULE)
        canv.setLineWidth(0.6)
        canv.line(doc.leftMargin, 0.6 * inch, w - doc.rightMargin, 0.6 * inch)
        canv.setFont('Sans', 8)
        canv.setFillColor(MUTED)
        canv.drawString(doc.leftMargin, 0.45 * inch, 'Lumora user guide  ·  version %s' % doc.version)
        canv.drawRightString(w - doc.rightMargin, 0.45 * inch, str(doc.page))
        canv.restoreState()

    def front_page(self, canv, doc):
        self._footer(canv, doc)

    def body_page(self, canv, doc):
        self._footer(canv, doc)
        if self.chapter:
            w, h = doc.pagesize
            canv.saveState()
            canv.setFont('Sans', 8)
            canv.setFillColor(MUTED)
            canv.drawRightString(w - doc.rightMargin, h - 0.6 * inch, self.chapter)
            canv.linkRect('', 'contents', (w - doc.rightMargin - 160, h - 0.66 * inch, w - doc.rightMargin,
                                           h - 0.5 * inch), relative=0, thickness=0)
            canv.drawString(doc.leftMargin, h - 0.6 * inch, 'Contents')
            canv.linkRect('', 'contents', (doc.leftMargin, h - 0.66 * inch, doc.leftMargin + 50,
                                           h - 0.5 * inch), relative=0, thickness=0)
            canv.restoreState()


# ---------------------------------------------------------------- block parser

def parse(md):
    """Yields (kind, data) blocks from the markdown source."""
    lines = md.splitlines()
    i = 0
    para = []

    def flush():
        if para:
            yield_list.append(('para', ' '.join(para)))
            para.clear()

    yield_list = []
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if stripped.startswith('```'):
            flush()
            body = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith('```'):
                body.append(lines[i])
                i += 1
            yield_list.append(('code', '\n'.join(body)))
        elif not stripped:
            flush()
        elif re.match(r'#{1,4} ', line):
            flush()
            hashes, rest = line.split(' ', 1)
            m = re.match(r'(.*?)\s*\{#([\w-]+)\}\s*$', rest)
            title, anchor = (m.group(1), m.group(2)) if m else (rest.strip(), None)
            yield_list.append(('h%d' % len(hashes), (title, anchor)))
        elif stripped.startswith('|'):
            flush()
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r':?-{3,}:?', c) for c in cells):
                    rows.append(cells)
                i += 1
            yield_list.append(('table', rows))
            continue
        elif stripped.startswith('!['):
            flush()
            m = re.match(r'!\[(.*?)\]\((.*?)\)', stripped)
            yield_list.append(('image', (m.group(1), m.group(2))))
        elif stripped.startswith('>'):
            flush()
            body = []
            while i < len(lines) and lines[i].strip().startswith('>'):
                body.append(lines[i].strip()[1:].strip())
                i += 1
            yield_list.append(('callout', ' '.join(body)))
            continue
        elif re.match(r'- ', stripped):
            flush()
            yield_list.append(('bullet', stripped[2:]))
            # continuation lines
            while i + 1 < len(lines) and lines[i + 1].startswith('  ') and lines[i + 1].strip():
                i += 1
                kind, text = yield_list[-1]
                yield_list[-1] = (kind, text + ' ' + lines[i].strip())
        elif re.match(r'\d+\. ', stripped):
            flush()
            num, text = stripped.split(' ', 1)
            yield_list.append(('number', (num, text)))
            while i + 1 < len(lines) and lines[i + 1].startswith('   ') and lines[i + 1].strip():
                i += 1
                kind, (n, t) = yield_list[-1]
                yield_list[-1] = (kind, (n, t + ' ' + lines[i].strip()))
        else:
            para.append(stripped)
        i += 1
    flush()
    return yield_list


# ---------------------------------------------------------------- story

def build_story(blocks, s, doc):
    story = []
    frame_w = doc.width
    part_no = 0
    chap_no = 0
    used = set()

    # Collect structure first: chapters per part and sections per chapter, for the link lists.
    parts = []
    for kind, data in blocks:
        if kind == 'h1':
            parts.append({'title': data[0], 'chapters': []})
        elif kind == 'h2':
            parts[-1]['chapters'].append({'title': data[0], 'anchor': data[1] or slug(data[0]), 'sections': []})
        elif kind == 'h3':
            parts[-1]['chapters'][-1]['sections'].append((data[0], data[1]))

    def section_key(title, anchor):
        key = anchor or 'sec-' + slug(title)
        base, n = key, 2
        while key in used:
            key = '%s-%d' % (base, n)
            n += 1
        used.add(key)
        return key

    # Cover and contents
    story.append(NextPageTemplate('front'))
    story.append(PageBreak())
    toc = TableOfContents(levelStyles=[s['toc0'], s['toc1']], dotsMinLevel=1)
    story.append(Paragraph('<a name="contents"/>Contents', s['h1']))
    story.append(Paragraph('Click any line to jump to it. In the guide, <b>Contents</b> at the top of each '
                           'page brings you back here.', s['inchapter']))
    story.append(toc)
    story.append(NextPageTemplate('body'))

    part_iter = iter(parts)
    current_part = None
    chap_in_part = None
    sections = []
    pending_keys = {}
    for kind, data in blocks:
        if kind == 'h1':
            current_part = next(part_iter)
            chap_in_part = iter(current_part['chapters'])
            part_no += 1
            key = 'part-%d' % part_no
            used.add(key)
            story.append(PageBreak())
            story.append(Spacer(1, 1.6 * inch))
            story.append(Paragraph('PART %d' % part_no, s['partnum']))
            story.append(PartHeading(inline(data[0]), s['parttitle'], 0, key,
                                     toc_text='Part %d · %s' % (part_no, esc(data[0])),
                                     outline_text='Part %d: %s' % (part_no, data[0])))
            for ch, n in zip(current_part['chapters'], range(chap_no + 1, chap_no + 1 + len(current_part['chapters']))):
                story.append(Paragraph('<font color="#E0A030"><b>%d</b></font>&#160;&#160;<a href="#%s" color="#1F6FB8">%s</a>'
                                       % (n, ch['anchor'], esc(ch['title'])), s['partlink']))
        elif kind == 'h2':
            chapter = next(chap_in_part)
            chap_no += 1
            key = data[1] or slug(data[0])
            used.add(key)
            story.append(PageBreak())
            story.append(Paragraph('CHAPTER %d' % chap_no, s['chapnum']))
            story.append(Heading(inline(data[0]), s['h1'], 1, key,
                                 toc_text='%d.&#160;&#160;%s' % (chap_no, esc(data[0])),
                                 outline_text='%d. %s' % (chap_no, data[0])))
            if len(chapter['sections']) >= 2:
                keys = []
                for title, anchor in chapter['sections']:
                    k = section_key(title, anchor)
                    keys.append(k)
                    pending_keys[(chap_no, title)] = k
                links = '  ·  '.join('<a href="#%s" color="#1F6FB8">%s</a>' % (k, esc(t))
                                     for (t, _), k in zip(chapter['sections'], keys))
                story.append(Paragraph('<b>In this chapter:</b>  ' + links, s['inchapter']))
            story.append(_rule(frame_w))
        elif kind == 'h3':
            key = pending_keys.get((chap_no, data[0])) or section_key(data[0], data[1])
            story.append(CondPageBreak(1.6 * inch))
            story.append(Heading(inline(data[0]), s['h2'], 2, key))
        elif kind == 'h4':
            story.append(CondPageBreak(0.8 * inch))
            story.append(Paragraph(inline(data[0]), s['h3']))
        elif kind == 'para':
            story.append(Paragraph(inline(data), s['body']))
        elif kind == 'bullet':
            story.append(Paragraph(inline(data), s['bullet'], bulletText='•'))
        elif kind == 'number':
            story.append(Paragraph(inline(data[1]), s['number'], bulletText=data[0]))
        elif kind == 'code':
            wrapped = []
            for ln in data.split('\n'):
                while len(ln) > 88:
                    cut = ln.rfind(' ', 0, 88)
                    cut = cut if cut > 20 else 88
                    wrapped.append(ln[:cut] + ' \\' if ln.lstrip().startswith(('cmake', 'sudo', 'brew')) else ln[:cut])
                    ln = '    ' + ln[cut:].lstrip()
                wrapped.append(ln)
            pre = Preformatted('\n'.join(wrapped), s['code'])
            t = Table([[pre]], colWidths=[frame_w])
            t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), CODE_BG),
                                   ('BOX', (0, 0), (-1, -1), 0.5, RULE),
                                   ('LEFTPADDING', (0, 0), (-1, -1), 9), ('RIGHTPADDING', (0, 0), (-1, -1), 9),
                                   ('TOPPADDING', (0, 0), (-1, -1), 6), ('BOTTOMPADDING', (0, 0), (-1, -1), 7)]))
            story.append(t)
            story.append(Spacer(1, 8))
        elif kind == 'callout':
            note = data.startswith('**Note')
            t = Table([[Paragraph(inline(data), s['callout'])]], colWidths=[frame_w])
            t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), NOTE_BG if note else TIP_BG),
                                   ('LINEBEFORE', (0, 0), (0, -1), 3, LINK if note else ACCENT),
                                   ('LEFTPADDING', (0, 0), (-1, -1), 11), ('RIGHTPADDING', (0, 0), (-1, -1), 10),
                                   ('TOPPADDING', (0, 0), (-1, -1), 7), ('BOTTOMPADDING', (0, 0), (-1, -1), 8)]))
            story.append(t)
            story.append(Spacer(1, 9))
        elif kind == 'table':
            story.append(_table(data, s, frame_w))
            story.append(Spacer(1, 9))
        elif kind == 'image':
            story.append(_image(data, s, frame_w, doc.height))
    return story


def _rule(width):
    t = Table([['']], colWidths=[width], rowHeights=[2])
    t.setStyle(TableStyle([('LINEABOVE', (0, 0), (-1, 0), 1.2, ACCENT)]))
    return KeepTogether([t, Spacer(1, 8)])


def _table(rows, s, frame_w):
    ncol = max(len(r) for r in rows)
    rows = [r + [''] * (ncol - len(r)) for r in rows]
    # Column widths follow how much text each column holds, within limits.
    lengths = [max(len(re.sub(r'[*`]', '', r[c])) for r in rows) for c in range(ncol)]
    weights = [min(max(l, 6), 70) for l in lengths]
    if ncol == 2:
        weights[0] = min(weights[0], 30)
    total = sum(weights)
    widths = [frame_w * w / total for w in weights]
    # No column narrower than its longest word, so words never split mid-letter.
    need = [max(pdfmetrics.stringWidth(word, 'Sans-Bold', 9) + 14
                for r in rows for word in (re.sub(r'[*`]', '', r[c]).split() or [''])) for c in range(ncol)]
    for c in range(ncol):
        if widths[c] < need[c]:
            short = need[c] - widths[c]
            widths[c] = need[c]
            widest = max(range(ncol), key=lambda k: widths[k] - need[k])
            widths[widest] -= short
    data = []
    for ri, r in enumerate(rows):
        data.append([Paragraph(inline(c), s['cellhead'] if ri == 0 else s['cell']) for c in r])
    t = Table(data, colWidths=widths, repeatRows=1)
    style = [('BACKGROUND', (0, 0), (-1, 0), DARK),
             ('VALIGN', (0, 0), (-1, -1), 'TOP'),
             ('LINEBELOW', (0, 1), (-1, -1), 0.4, RULE),
             ('LEFTPADDING', (0, 0), (-1, -1), 6), ('RIGHTPADDING', (0, 0), (-1, -1), 6),
             ('TOPPADDING', (0, 0), (-1, -1), 4), ('BOTTOMPADDING', (0, 0), (-1, -1), 5)]
    for ri in range(2, len(data), 2):
        style.append(('BACKGROUND', (0, ri), (-1, ri), colors.HexColor('#F7F8FA')))
    t.setStyle(TableStyle(style))
    return t


def _image(data, s, frame_w, frame_h):
    caption, path = data
    full = os.path.join(HERE, path)
    if not os.path.exists(full):
        print('warning: missing image', path)
        return Paragraph('<i>[missing picture: %s]</i>' % esc(path), s['caption'])
    from reportlab.lib.utils import ImageReader
    iw, ih = ImageReader(full).getSize()
    w = min(frame_w, iw * 0.55)
    h = w * ih / iw
    if h > frame_h * 0.55:
        h = frame_h * 0.55
        w = h * iw / ih
    img = Image(full, width=w, height=h)
    frame = Table([[img]], colWidths=[w + 2])
    frame.setStyle(TableStyle([('BOX', (0, 0), (-1, -1), 0.6, RULE),
                               ('LEFTPADDING', (0, 0), (-1, -1), 1), ('RIGHTPADDING', (0, 0), (-1, -1), 1),
                               ('TOPPADDING', (0, 0), (-1, -1), 1), ('BOTTOMPADDING', (0, 0), (-1, -1), 1)]))
    parts = [Spacer(1, 3), frame]
    parts.append(Paragraph(inline(caption), s['caption']) if caption else Spacer(1, 10))
    return KeepTogether(parts)


def check_links(blocks):
    anchors = {d[1] for k, d in blocks if k in ('h2', 'h3') and d[1]}
    bad = []
    for kind, data in blocks:
        texts = []
        if kind in ('para', 'bullet', 'callout'):
            texts = [data]
        elif kind == 'number':
            texts = [data[1]]
        elif kind == 'table':
            texts = [c for r in data for c in r]
        for t in texts:
            bad += [a for a in re.findall(r'\]\(#([\w-]+)\)', t) if a not in anchors]
    return bad


def main():
    version = app_version()
    register_fonts()
    with open(SOURCE, encoding='utf-8') as f:
        md = f.read().replace('{{version}}', version)
    blocks = parse(md)
    bad = check_links(blocks)
    if bad:
        sys.exit('broken links in manual.md: %s' % ', '.join(sorted(set(bad))))
    out = sys.argv[1] if len(sys.argv) > 1 else OUTPUT
    doc = ManualDoc(out, version)
    story = build_story(blocks, styles(), doc)
    doc.multiBuild(story)
    with open(STAMP, 'w', encoding='utf-8') as f:
        f.write(version + '\n')
    print('wrote %s (version %s, %d pages)' % (out, version, doc.page))
    return 0


if __name__ == '__main__':
    sys.exit(main())
