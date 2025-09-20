#!/usr/bin/env node

import { readFileSync } from 'fs'
import { createHighlighter } from 'shiki'

const janet = JSON.parse(readFileSync('assets/janet.tmLanguage.json', 'utf8'))

const highlighter = await createHighlighter({
    langs: [ 'shell',  janet ],
    themes: ['solarized-light']
})

function highlightFilter(node) {
    if (node.t === 'CodeBlock') {
        const [[, classes, ], code] = node.c

        const html = highlighter.codeToHtml(code, {
            lang: classes[0],
            theme: 'solarized-light'
        })

        return { t: 'RawBlock', c: ['html', html]}
    }

    return node
}

const input = JSON.parse(readFileSync(0, 'utf8'))
for (const [index, block] of input.blocks.entries()) {
    input.blocks[index] = highlightFilter(block)
}

process.stdout.write(JSON.stringify(input))
