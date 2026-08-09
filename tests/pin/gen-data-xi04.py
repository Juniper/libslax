#!/usr/bin/env python3
"""
Generate xi04.02.in: 10000 pa_istr_intern_string operations over ~1081 unique
strings, with a deterministic random seed so the file is reproducible.

Usage:
    python3 make-xi04-02.py > xi04.02.in
"""

import random
import sys

rng = random.Random(42)

base = [
    # from xi04.01.in
    'div', 'span', 'p', 'ul', 'ol', 'li', 'table', 'tr', 'td', 'th',
    'a', 'img', 'h1', 'h2', 'class', 'id', 'href', 'src', 'alt', 'type',
    'name', 'value', 'style', 'xml', 'xsl', 'xs', 'xmlns', 'text/html',
    'text/xml', 'UTF-8', 'onclick', 'onchange', 'form', 'input', 'select',
    # additional HTML elements
    'html', 'head', 'body', 'h3', 'h4', 'h5', 'h6',
    'header', 'footer', 'nav', 'main', 'aside', 'section', 'article',
    'figure', 'figcaption', 'blockquote', 'pre', 'code',
    'em', 'strong', 'small', 'sup', 'sub', 's', 'u', 'b', 'mark',
    'cite', 'q', 'abbr', 'time', 'address',
    'textarea', 'button', 'label', 'dl', 'dt', 'dd',
    'fieldset', 'legend', 'datalist', 'optgroup', 'output',
    'progress', 'meter', 'canvas', 'svg', 'video', 'audio',
    'source', 'track', 'picture', 'script', 'link', 'meta', 'title',
    'base', 'noscript', 'template', 'slot',
    'details', 'summary', 'dialog', 'menu',
    'caption', 'colgroup', 'col', 'thead', 'tbody', 'tfoot',
    'iframe', 'object', 'param', 'embed', 'map', 'area',
    'bdi', 'bdo', 'wbr', 'ruby', 'rt', 'rp', 'ins', 'del',
    # HTML attributes
    'placeholder', 'readonly', 'disabled', 'checked', 'selected',
    'multiple', 'size', 'maxlength', 'rows', 'cols',
    'colspan', 'rowspan', 'action', 'method', 'enctype', 'target',
    'rel', 'media', 'charset', 'content', 'http-equiv',
    'lang', 'dir', 'tabindex', 'accesskey', 'hidden',
    'contenteditable', 'draggable', 'spellcheck', 'role', 'for',
    'autocomplete', 'autofocus', 'pattern', 'min', 'max', 'step',
    'list', 'required', 'novalidate', 'accept', 'capture',
    'width', 'height', 'border', 'cellpadding', 'cellspacing',
    'bgcolor', 'align', 'valign', 'wrap', 'open', 'reversed', 'start',
    'controls', 'autoplay', 'loop', 'muted', 'preload', 'poster',
    'srcset', 'sizes', 'decoding', 'loading', 'crossorigin',
    'integrity', 'referrerpolicy', 'fetchpriority',
    'onsubmit', 'onreset', 'onload', 'onunload', 'onresize', 'onscroll',
    'onkeydown', 'onkeyup', 'onkeypress',
    'onmousedown', 'onmouseup', 'onmouseover', 'onmouseout',
    'onmouseenter', 'onmouseleave', 'onfocus', 'onblur', 'onselect',
    'ondblclick', 'onpaste', 'oncopy', 'oncut',
    'ondrag', 'ondragstart', 'ondragend', 'ondragover',
    'ondragenter', 'ondragleave', 'ondrop',
    'oncontextmenu', 'onerror', 'onabort',
    'onplay', 'onpause', 'onended', 'onprogress', 'onwaiting',
    'aria-label', 'aria-hidden', 'aria-describedby', 'aria-labelledby',
    'aria-expanded', 'aria-selected', 'aria-checked', 'aria-disabled',
    'aria-pressed', 'aria-required', 'aria-invalid', 'aria-live',
    'data-id', 'data-name', 'data-value', 'data-type', 'data-index',
    # namespace / MIME types
    'xhtml', 'dc', 'rdf', 'rdfs', 'owl', 'foaf', 'atom', 'soap',
    'wsdl', 'xsd', 'svgns', 'mathml', 'xlink', 'xi', 'xptr',
    'application/json', 'application/xml', 'application/xhtml+xml',
    'text/plain', 'text/css', 'text/javascript',
    'image/png', 'image/jpeg', 'image/svg+xml',
    'ISO-8859-1', 'UTF-16',
    # CSS properties
    'color', 'background', 'background-color', 'background-image',
    'background-position', 'background-size', 'background-repeat',
    'border-radius', 'border-color', 'border-width', 'border-style',
    'padding', 'padding-top', 'padding-right', 'padding-bottom', 'padding-left',
    'margin', 'margin-top', 'margin-right', 'margin-bottom', 'margin-left',
    'font-size', 'font-family', 'font-weight', 'font-style', 'font-variant',
    'text-align', 'text-decoration', 'text-transform', 'line-height',
    'letter-spacing', 'word-spacing', 'display', 'position',
    'top', 'right', 'bottom', 'left', 'float', 'clear',
    'overflow', 'z-index', 'opacity', 'visibility', 'cursor',
    'flex', 'flex-direction', 'justify-content', 'align-items', 'flex-wrap',
    'grid', 'grid-template', 'gap', 'transition', 'animation',
    'transform', 'box-shadow', 'text-shadow', 'list-style',
    'outline', 'resize', 'pointer-events', 'user-select',
]

# Numbered strings to fill out the unique set
for i in range(2500):
    base.append('elt-%04d' % i)
for i in range(1500):
    base.append('att-%04d' % i)
for i in range(1000):
    base.append('data-%04d' % i)

# Deduplicate preserving first-seen order
seen = set()
unique = []
for s in base:
    if s not in seen:
        seen.add(s)
        unique.append(s)

random.shuffle(unique)

n_unique = len(unique)
n_total = 10000
n_extra = n_total - n_unique

print('# clean file /tmp/xi04-02.db')
for s in unique:
    print('i', s)
for s in rng.choices(unique, k=n_extra):
    print('i', s)
print('d')

print(f'# {n_unique} unique strings, {n_total} total operations', file=sys.stderr)
