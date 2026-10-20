import { createContext, useContext } from 'react'
import type { ReactNode } from 'react'
import { usePersistedFlag } from '@ui/hooks/useStorage'

/*
 * The app's own choices about how a field is drawn, remembered on this computer. A descriptor field says what it may be drawn as
 * (`control`: slider, switch); these say whether the user wants that. Both are on until the user turns them off.
 */

export interface AppPrefs {
  /** A number with a range (`control: slider`) is a slider, not a number field. */
  readonly sliders: boolean
  /** A choice of two (`control: switch`) is a switch, not a list. */
  readonly switches: boolean
  setSliders(on: boolean): void
  setSwitches(on: boolean): void
}

const Prefs = createContext<AppPrefs>({ sliders: true, switches: true, setSliders: () => undefined, setSwitches: () => undefined })

export const useAppPrefs = (): AppPrefs => useContext(Prefs)

export function AppPrefsProvider({ children }: { children: ReactNode }) {
  const [sliders, setSliders] = usePersistedFlag('runit.app.sliders', true)
  const [switches, setSwitches] = usePersistedFlag('runit.app.switches', true)
  return <Prefs.Provider value={{ sliders, switches, setSliders, setSwitches }}>{children}</Prefs.Provider>
}
