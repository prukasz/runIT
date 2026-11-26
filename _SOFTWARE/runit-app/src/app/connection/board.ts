/*
 * The user's board as the app remembers it, pure. Every board advertises the same name by default (`CONFIG_SYS_BLE_DEVICE_NAME`,
 * "runit"), so the name says what to show and the address says which board it is: the desktop app finds the board by its
 * address (its scan reports it), a browser by its own id for it (`BluetoothDevice.id`; a browser never shows the address).
 */

export interface RememberedBoard {
  /** The name it advertised when it was last found. */
  readonly name: string
  /** Its Bluetooth address (`94:A9:90:DB:32:3E`): the desktop app's scan reports it. */
  readonly address?: string
  /** The browser's id for it (`BluetoothDevice.id`), random per site: what a browser opens it by again. */
  readonly browserId?: string
}

const ADDRESS = /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/

/** An address as the app keeps it (upper case, colons); undefined for anything that is not one. */
export const normalAddress = (text: string): string | undefined => {
  const address = text.trim().toUpperCase()
  return ADDRESS.test(address) ? address : undefined
}

/** The remembered board from storage; undefined when it is not one (a board needs a name and a way to find it). */
export const parseRemembered = (raw: string): RememberedBoard | undefined => {
  const value: unknown = JSON.parse(raw)
  if (typeof value !== 'object' || value === null) return undefined
  const { name, address, browserId } = value as Record<string, unknown>
  const at = typeof address === 'string' ? normalAddress(address) : undefined
  const id = typeof browserId === 'string' && browserId ? browserId : undefined
  if (typeof name !== 'string' || (!at && !id)) return undefined
  return { name, ...(at ? { address: at } : {}), ...(id ? { browserId: id } : {}) }
}

/** The board as the panel names it: its name, and its address when known. */
export const boardLabel = (board: RememberedBoard): string => (board.address ? `${board.name} (${board.address})` : board.name)
