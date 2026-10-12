import { Gauge, Hash, BarChart2, Joystick, Keyboard, Lightbulb, LineChart, MousePointerClick, Puzzle, SlidersHorizontal, TextCursorInput, ToggleLeft, Type } from 'lucide-react'
import type { LucideIcon } from 'lucide-react'
import { isBuiltinKind } from '../domain/controller'
import type { WidgetKind } from '../domain/controller'

const BUILTIN: Readonly<Record<WidgetKind, LucideIcon>> = {
  joystick: Joystick, slider: SlidersHorizontal, switch: ToggleLeft, button: MousePointerClick, input: TextCursorInput, keyboard: Keyboard,
  display: Hash, chart: LineChart, lamp: Lightbulb, bar: BarChart2, gauge: Gauge, label: Type,
}

/** A widget's icon; an imported (or missing) one has the puzzle piece. */
export const widgetIcon = (kind: string): LucideIcon => (isBuiltinKind(kind) ? BUILTIN[kind] : Puzzle)
