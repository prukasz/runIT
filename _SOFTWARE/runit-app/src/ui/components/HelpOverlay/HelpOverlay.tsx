import { useEffect, useState } from 'react'
import './HelpOverlay.css'

/** An element marked for help: where it is on the screen now, and the text it carries (`data-help`). */
interface Mark { readonly text: string; readonly top: number; readonly left: number; readonly width: number; readonly height: number }

/** What has help: an element with a `data-help` text, and a button with a title (the Help button itself is left out). */
const HELPED = '.shell [data-help], .shell button[title]'

/** The elements that have help and are on the screen now, in the order of the page. */
const measure = (): Mark[] =>
  [...document.querySelectorAll<HTMLElement>(HELPED)].flatMap((el) => {
    if (el.classList.contains('help-toggle') || el.closest('.help-overlay')) return []
    // A control inside a form row is explained by that row.
    if (!el.hasAttribute('data-help') && el.closest('.form-row[data-help]')) return []
    const text = (el.dataset.help ?? el.getAttribute('title') ?? '').trim()
    const r = el.getBoundingClientRect()
    const shown = r.width > 0 && r.height > 0 && r.bottom > 0 && r.right > 0 && r.top < innerHeight && r.left < innerWidth
    return text && shown ? [{ text, top: r.top, left: r.left, width: r.width, height: r.height }] : []
  })

/**
 * Help mode: each element of the shell that has help is outlined. A click on an outline shows its text beside it, and
 * a click elsewhere (or Escape) hides it. While help mode is on, the screen does not take clicks: an outline or the text is the
 * only thing a click reaches, and the Help button itself (`.help-toggle`) still turns it off.
 */
export function HelpOverlay({ on }: { on: boolean }) {
  const [marks, setMarks] = useState<readonly Mark[]>([])
  const [open, setOpen] = useState<string | undefined>()
  useEffect(() => {
    if (!on) return
    // Measured on a timer as well: a screen can change without a resize, and an unchanged list is not set again.
    let last = ''
    const update = () => {
      const next = measure()
      const key = JSON.stringify(next)
      if (key !== last) { last = key; setMarks(next) }
    }
    update()
    const timer = window.setInterval(update, 250)
    window.addEventListener('resize', update)
    window.addEventListener('scroll', update, true)
    return () => {
      window.clearInterval(timer)
      window.removeEventListener('resize', update)
      window.removeEventListener('scroll', update, true)
    }
  }, [on])
  useEffect(() => {
    if (!on) return
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') setOpen(undefined) }
    window.addEventListener('keydown', escape)
    return () => window.removeEventListener('keydown', escape)
  }, [on])
  if (!on) return null
  const shown = marks.find((mark) => mark.text === open)
  return (
    <div className="help-overlay">
      <div className="help-catch" onClick={() => setOpen(undefined)} />
      {marks.map((mark, i) => (
        <button key={`${i}:${mark.text}`} type="button" className={`help-box ${mark.text === open ? 'is-open' : ''}`} aria-label={mark.text} aria-expanded={mark.text === open}
          style={{ top: mark.top, left: mark.left, width: mark.width, height: mark.height }}
          onClick={() => setOpen(mark.text === open ? undefined : mark.text)} />
      ))}
      {shown && <HelpCard mark={shown} />}
    </div>
  )
}

/** The text of a marked element, beside it: below it, or above it when it is in the lower half of the screen. */
function HelpCard({ mark }: { mark: Mark }) {
  const lower = mark.top + mark.height / 2 > innerHeight / 2
  const side = { left: Math.max(8, Math.min(mark.left, innerWidth - 316)) }
  return (
    <div className="help-card" role="note" style={lower ? { ...side, bottom: innerHeight - mark.top + 8 } : { ...side, top: mark.top + mark.height + 8 }}>
      {mark.text}
    </div>
  )
}
