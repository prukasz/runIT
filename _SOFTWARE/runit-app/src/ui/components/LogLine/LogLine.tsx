import './LogLine.css'

/** The log levels of the board's lines (ESP_LOG's letter), and the app's own notes. */
export type LogLevel = 'E' | 'W' | 'I' | 'D' | 'V' | 'app'

/** One line of the console: when, its level, where from (the log tag), the text. Monospace, coloured by level. */
export function LogLine({ time, level, source, text }: { time?: string; level: LogLevel; source?: string; text: string }) {
  return (
    <div className={`log-line is-${level === 'app' ? 'app' : level.toLowerCase()}`}>
      {time && <span className="log-time">{time}</span>}
      <span className="log-level">{level === 'app' ? '·' : level}</span>
      {source && <span className="log-source">{source}</span>}
      <span className="log-text">{text}</span>
    </div>
  )
}
