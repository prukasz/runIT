import ReactMarkdown from 'react-markdown'
import type { Components } from 'react-markdown'
import remarkGfm from 'remark-gfm'
import './Markdown.css'

/*
 * Device guides (app/docs/devices/<descriptor id>.md) as GitHub-flavoured
 * Markdown: headings, lists, task lists, tables, quotes, code, links, images.
 * Raw HTML in the source is not rendered.
 *
 * Images: put them under app/docs/devices/ (e.g. images/device_pca9685/wiring.png)
 * and write the path relative to that folder: ![Wiring](images/device_pca9685/wiring.png "Caption").
 * Vite bundles them, so a guide works offline. http(s) URLs are shown as they are.
 */

/** Images next to the guides, by their path relative to app/docs/devices/. */
const IMAGES: Readonly<Record<string, string>> = Object.fromEntries(
  Object.entries(import.meta.glob<string>('../../docs/devices/**/*.{png,jpg,jpeg,gif,svg,webp}', { query: '?url', import: 'default', eager: true }))
    .map(([path, url]) => [path.replace(/^.*?\/docs\/devices\//, ''), url]),
)

const imageUrl = (src: string | undefined): string | undefined => {
  if (!src) return undefined
  if (/^https?:\/\//i.test(src)) return src
  return IMAGES[decodeURI(src).replace(/^\.\//, '')]
}

const COMPONENTS: Components = {
  a: ({ href, children }) => (href?.startsWith('#') ? <a href={href}>{children}</a> : <a href={href} target="_blank" rel="noreferrer">{children}</a>),
  img: ({ src, alt, title }) => {
    const url = imageUrl(typeof src === 'string' ? src : undefined)
    if (!url) return <span className="device-markdown-missing">{`Image not found: ${String(src ?? '')}`}</span>
    return (
      <span className="device-markdown-figure">
        <img src={url} alt={alt ?? ''} loading="lazy" />
        {title && <span className="device-markdown-caption">{title}</span>}
      </span>
    )
  },
  table: ({ children }) => <div className="device-markdown-table"><table>{children}</table></div>,
}

export function Markdown({ source }: { source: string }) {
  return (
    <div className="device-markdown">
      <ReactMarkdown remarkPlugins={[remarkGfm]} components={COMPONENTS}>{source}</ReactMarkdown>
    </div>
  )
}
