import { useState } from 'react'
import { usePersistedChoice, readStored, writeStored } from '@ui/hooks/useStorage'
import { SLOW_FACTORS } from '../useVm'
import type { Stepping } from '../useVm'

/** How long a block's pulse and a changed value stay lit, ms: a pass lasts a few of them, an eye needs a few hundred. */
export const HOLD_DEFAULT_MS = 500
export const HOLD_MAX_MS = 10000

/** Program time one pass asked for adds while stepping, ms: a pass at full speed is about 10, a step you can follow is longer. */
export const STEP_MS_DEFAULT = 100
export const STEP_MS_MAX = 60000

const STEPPINGS = ['continuous', 'loop', 'step'] as const satisfies readonly Stepping[]
const SLOW = SLOW_FACTORS.map(String)

/** What the debug panel remembers: how long pulses are held, the debug run's slow motion and how it steps (browser storage, a convenience). */
export function useRunSettings() {
  const [stepping, setStepping] = usePersistedChoice<Stepping>('runit.run.stepping', STEPPINGS, 'continuous')
  const [holdMs, setHold] = useState(() => readStored('runit.run.holdMs', (raw) => {
    const n = Number(raw)
    return Number.isFinite(n) && n >= 0 && n <= HOLD_MAX_MS ? n : undefined
  }, HOLD_DEFAULT_MS))
  const [stepMs, setStep] = useState(() => readStored('runit.run.stepMs', (raw) => {
    const n = Number(raw)
    return Number.isInteger(n) && n >= 0 && n <= STEP_MS_MAX ? n : undefined
  }, STEP_MS_DEFAULT))
  const [slowText, setSlowText] = usePersistedChoice('runit.run.slow', SLOW, '1')
  return {
    stepping,
    setStepping,
    holdMs,
    setHoldMs: (ms: number) => { setHold(ms); writeStored('runit.run.holdMs', String(ms)) },
    stepMs,
    setStepMs: (ms: number) => { setStep(ms); writeStored('runit.run.stepMs', String(ms)) },
    slow: Number(slowText),
    setSlow: (factor: number) => setSlowText(String(factor)),
  }
}

export type RunSettings = ReturnType<typeof useRunSettings>
