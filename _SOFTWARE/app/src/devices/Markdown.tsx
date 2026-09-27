import type { ReactNode } from 'react'

/*
 * A small Markdown subset for device guides (app/docs/devices/*.md):
 * headings, paragraphs, bullet and numbered lists, fenced code, and inline
 * `code`, **bold** and [links](url). Built as React nodes, never as HTML.
 */

const inline = (text: string, key: string): ReactNode[] => {
  const out: ReactNode[] = []
  const pattern = /(`[^`]+`)|(\*\*[^*]+\*\*)|(\[[^\]]+\]\([^)\s]+\))/g
  let last = 0
  let index = 0
  for (const match of text.matchAll(pattern)) {
    if (match.index > last) out.push(text.slice(last, match.index))
    const token = match[0]
    const id = `${key}-${index++}`
    if (token.startsWith('`')) out.push(<code key={id}>{token.slice(1, -1)}</code>)
    else if (token.startsWith('**')) out.push(<strong key={id}>{token.slice(2, -2)}</strong>)
    else {
      const [, label, href] = /^\[([^\]]+)\]\(([^)\s]+)\)$/.exec(token)!
      const safe = /^(https?:|mailto:|#)/i.test(href!) ? href : undefined
      out.push(safe ? <a key={id} href={safe} target="_blank" rel="noreferrer">{label}</a> : label)
    }
    last = match.index + token.length
  }
  if (last < text.length) out.push(text.slice(last))
  return out
}

export function Markdown({ source }: { source: string }) {
  const blocks: ReactNode[] = []
  const lines = source.replace(/\r\n/g, '\n').split('\n')
  let paragraph: string[] = []
  let list: { ordered: boolean; items: string[] } | undefined
  const flush = () => {
    const key = `b${blocks.length}`
    if (paragraph.length) blocks.push(<p key={key}>{inline(paragraph.join(' '), key)}</p>)
    else if (list) {
      const items = list.items.map((item, index) => <li key={index}>{inline(item, `${key}-${index}`)}</li>)
      blocks.push(list.ordered ? <ol key={key}>{items}</ol> : <ul key={key}>{items}</ul>)
    }
    paragraph = []
    list = undefined
  }
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index]!
    if (line.startsWith('```')) {
      flush()
      const code: string[] = []
      while (++index < lines.length && !lines[index]!.startsWith('```')) code.push(lines[index]!)
      blocks.push(<pre key={`b${blocks.length}`}><code>{code.join('\n')}</code></pre>)
      continue
    }
    const heading = /^(#{1,3})\s+(.*)$/.exec(line)
    const bullet = /^\s*[-*]\s+(.*)$/.exec(line)
    const numbered = /^\s*\d+\.\s+(.*)$/.exec(line)
    if (heading) {
      flush()
      const key = `b${blocks.length}`
      const content = inline(heading[2]!, key)
      blocks.push(heading[1]!.length === 1 ? <h2 key={key}>{content}</h2> : heading[1]!.length === 2 ? <h3 key={key}>{content}</h3> : <h4 key={key}>{content}</h4>)
    } else if (bullet || numbered) {
      const ordered = !!numbered
      if (paragraph.length || (list && list.ordered !== ordered)) flush()
      list ??= { ordered, items: [] }
      list.items.push((bullet ?? numbered)![1]!)
    } else if (!line.trim()) flush()
    else {
      if (list) flush()
      paragraph.push(line.trim())
    }
  }
  flush()
  return <div className="device-markdown">{blocks}</div>
}
