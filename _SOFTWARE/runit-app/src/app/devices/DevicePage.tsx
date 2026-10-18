import { Fragment, useState } from 'react'
import type { ReactNode } from 'react'
import { ArrowLeft, Languages } from 'lucide-react'
import { Button, DeviceTile, PanelHeader, SegmentedSwitch } from '@ui/components'
import { usePersistedChoice } from '@ui/hooks/useStorage'
import { manualOf, picturesOf, sourcesOf } from './deviceDocs'
import type { ManualLanguage } from './deviceDocs'
import type { DeviceInfo } from './devices'

/*
 * A device's page in the centre: its manual (markdown from the device's manual folder, with its pictures) and a copy of its own
 * code, read only. The markdown is shown as React elements: no HTML from the file is ever inserted.
 */

type Block =
  | { readonly kind: 'heading'; readonly level: number; readonly text: string }
  | { readonly kind: 'paragraph'; readonly text: string }
  | { readonly kind: 'list'; readonly ordered: boolean; readonly items: readonly string[] }
  | { readonly kind: 'code'; readonly text: string }
  | { readonly kind: 'picture'; readonly alt: string; readonly src: string }
  | { readonly kind: 'table'; readonly head: readonly string[]; readonly rows: readonly (readonly string[])[] }

const PICTURE = /^!\[([^\]]*)\]\(([^)]+)\)\s*$/
const ITEM = /^\s*([-*]|\d+\.)\s+(.*)$/

/** The blocks of a manual: headings, paragraphs, lists, code, and pictures on a line of their own. */
export const parseManual = (text: string): readonly Block[] => {
  const lines = text.replace(/\r\n/g, '\n').split('\n')
  const blocks: Block[] = []
  let i = 0
  while (i < lines.length) {
    const line = lines[i]!
    if (line.startsWith('```')) {
      const code: string[] = []
      i++
      while (i < lines.length && !lines[i]!.startsWith('```')) code.push(lines[i++]!)
      i++
      blocks.push({ kind: 'code', text: code.join('\n') })
      continue
    }
    const heading = line.match(/^(#{1,4})\s+(.*)$/)
    if (heading) {
      blocks.push({ kind: 'heading', level: heading[1]!.length, text: heading[2]! })
      i++
      continue
    }
    if (line.trim().startsWith('|') && i + 1 < lines.length && /^\s*\|?\s*:?-{2,}/.test(lines[i + 1]!)) {
      const cells = (text: string) => text.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map((c) => c.trim())
      const head = cells(line)
      i += 2
      const rows: string[][] = []
      while (i < lines.length && lines[i]!.trim().startsWith('|')) rows.push(cells(lines[i++]!))
      blocks.push({ kind: 'table', head, rows })
      continue
    }
    const picture = line.match(PICTURE)
    if (picture) {
      blocks.push({ kind: 'picture', alt: picture[1]!, src: picture[2]! })
      i++
      continue
    }
    const item = line.match(ITEM)
    if (item) {
      const ordered = /\d/.test(item[1]!)
      const items: string[] = []
      while (i < lines.length) {
        const next = lines[i]!.match(ITEM)
        if (next && /\d/.test(next[1]!) === ordered) {
          items.push(next[2]!)
        } else if (next || lines[i]!.trim() === '' || lines[i]!.startsWith('```') || /^#{1,4}\s/.test(lines[i]!) || PICTURE.test(lines[i]!) || items.length === 0) {
          break
        } else {
          // A line without a marker that follows an item carries on that item (markdown's lazy continuation).
          items[items.length - 1] += ' ' + lines[i]!.trim()
        }
        i++
      }
      blocks.push({ kind: 'list', ordered, items })
      continue
    }
    if (line.trim() === '') {
      i++
      continue
    }
    const paragraph: string[] = []
    while (i < lines.length && lines[i]!.trim() !== '' && !lines[i]!.startsWith('```') && !/^#{1,4}\s/.test(lines[i]!) && !ITEM.test(lines[i]!) && !PICTURE.test(lines[i]!) && !lines[i]!.trim().startsWith('|')) {
      paragraph.push(lines[i++]!.trim())
    }
    blocks.push({ kind: 'paragraph', text: paragraph.join(' ') })
  }
  return blocks
}

/** The text of a line with its inline marks: `code`, **bold**, *italic*, [links](url), and a picture inside it. */
const inline = (text: string, pictures: Readonly<Record<string, string>>): ReactNode[] => {
  const tokens = text.split(/(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*|!\[[^\]]*\]\([^)]+\)|\[[^\]]+\]\([^)]+\))/g)
  return tokens.map((token, i) => {
    if (!token) return null
    if (token.startsWith('`') && token.endsWith('`')) return <code key={i}>{token.slice(1, -1)}</code>
    if (token.startsWith('**') && token.endsWith('**')) return <strong key={i}>{token.slice(2, -2)}</strong>
    if (token.startsWith('*') && token.endsWith('*') && token.length > 2) return <em key={i}>{token.slice(1, -1)}</em>
    const picture = token.match(PICTURE)
    if (picture) return <Picture key={i} alt={picture[1]!} src={picture[2]!} pictures={pictures} />
    const link = token.match(/^\[([^\]]+)\]\(([^)]+)\)$/)
    if (link) return link[2]!.startsWith('http') ? <a key={i} href={link[2]} target="_blank" rel="noreferrer">{link[1]}</a> : <Fragment key={i}>{link[1]}</Fragment>
    return <Fragment key={i}>{token}</Fragment>
  })
}

/** A picture of the manual's folder by its file name; one that is not there says so. */
function Picture({ alt, src, pictures }: { alt: string; src: string; pictures: Readonly<Record<string, string>> }) {
  const name = src.split('/').pop() ?? src
  const url = pictures[name]
  return url
    ? <img className="devices-picture" src={url} alt={alt || name} />
    : <span className="devices-hint">Picture {name} is not in the manual's folder.</span>
}

/** The manual as the page shows it. */
export function Manual({ text, pictures }: { text: string; pictures: Readonly<Record<string, string>> }) {
  return (
    <div className="devices-manual">
      {parseManual(text).map((block, i) => {
        switch (block.kind) {
          case 'heading': return <div key={i} className={`devices-manual-h${block.level}`}>{inline(block.text, pictures)}</div>
          case 'paragraph': return <p key={i}>{inline(block.text, pictures)}</p>
          case 'list': {
            const List = block.ordered ? 'ol' : 'ul'
            return <List key={i}>{block.items.map((item, j) => <li key={j}>{inline(item, pictures)}</li>)}</List>
          }
          case 'code': return <pre key={i} className="devices-code">{block.text}</pre>
          case 'picture': return <Picture key={i} alt={block.alt} src={block.src} pictures={pictures} />
          case 'table': return (
            <table key={i} className="devices-manual-table">
              <thead><tr>{block.head.map((cell, j) => <th key={j}>{inline(cell, pictures)}</th>)}</tr></thead>
              <tbody>{block.rows.map((row, j) => <tr key={j}>{row.map((cell, k) => <td key={k}>{inline(cell, pictures)}</td>)}</tr>)}</tbody>
            </table>
          )
        }
      })}
    </div>
  )
}

const C_KEYWORDS = new Set(['if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default', 'break', 'continue', 'return', 'goto', 'static', 'const', 'extern', 'inline', 'sizeof', 'struct', 'union', 'enum', 'typedef', 'volatile', 'true', 'false', 'NULL'])
const C_TYPES = new Set(['void', 'bool', 'char', 'int', 'long', 'short', 'float', 'double', 'unsigned', 'signed', 'size_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'err_h'])
// Comments, strings, preprocessor lines, numbers, words: in that order, so a word inside a comment is not a keyword.
const C_TOKEN = /(\/\*[\s\S]*?\*\/|\/\/[^\n]*)|("(?:\.|[^"\\n])*"|'(?:\.|[^'\\n])*')|(^[ \t]*#[^\n]*)|(\b(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b)|([A-Za-z_]\w*)/gm

/** A C file as React elements, its tokens coloured by class (`tok-*`); nothing from the file is inserted as HTML. */
export const highlightC = (text: string): ReactNode[] => {
  const out: ReactNode[] = []
  let last = 0
  for (const match of text.matchAll(C_TOKEN)) {
    const at = match.index ?? 0
    if (at > last) out.push(text.slice(last, at))
    const [whole, comment, string, preprocessor, number, word] = match
    const kind = comment ? 'tok-com' : string ? 'tok-str' : preprocessor ? 'tok-pre' : number ? 'tok-num'
      : word ? (C_KEYWORDS.has(word) ? 'tok-kw' : C_TYPES.has(word) ? 'tok-type' : undefined) : undefined
    out.push(kind ? <span key={at} className={kind}>{whole}</span> : whole)
    last = at + whole.length
  }
  if (last < text.length) out.push(text.slice(last))
  return out
}

/** The device's page: the manual and its own code. `onBack` returns to its settings. */
export function DevicePage({ device, onBack }: { device: DeviceInfo; onBack: () => void }) {
  const [tab, setTab] = useState<'manual' | 'source'>('manual')
  // The language of the manual is the reader's choice, kept for the next device page.
  const [language, setLanguage] = usePersistedChoice<ManualLanguage>('runit.manual.language', ['en', 'pl'] as const, 'en')
  const id = device.type?.id ?? ''
  const manual = manualOf(id, language)
  const pictures = picturesOf(id)
  const code = sourcesOf(id)
  return (
    <>
      <PanelHeader icon={<DeviceTile tags={device.type?.tags} />} title={device.name}>
        <Button size="sm" onClick={onBack}><ArrowLeft aria-hidden="true" />Settings</Button>
        {tab === 'manual' && (
          <Button size="icon-sm" aria-label={`Language: ${language === 'pl' ? 'Polski' : 'English'}`}
            title={language === 'pl' ? 'Switch to English' : 'Przełącz na polski'} onClick={() => setLanguage(language === 'pl' ? 'en' : 'pl')}>
            <Languages aria-hidden="true" />
          </Button>
        )}
      </PanelHeader>
      <SegmentedSwitch label="Device page" value={tab} options={[['manual', 'Manual'], ['source', 'Source']]} onChange={setTab} />
      {tab === 'manual'
        ? (manual !== undefined
          ? (
            <>
              {manual.fallback && <p className="devices-hint">No {language === 'pl' ? 'Polish' : 'other'} version yet: English shown.</p>}
              <Manual text={manual.text} pictures={pictures} />
            </>
          )
          : <p className="devices-hint">No manual yet: write manual.md in the device's manual folder.</p>)
        : (code.length === 0
          ? <p className="devices-hint">No code copied for this device.</p>
          : code.map((file) => (
            <Fragment key={file.name}>
              <h4 className="devices-code-name">{file.name}</h4>
              <pre className="devices-code">{highlightC(file.text)}</pre>
            </Fragment>
          )))}
    </>
  )
}
