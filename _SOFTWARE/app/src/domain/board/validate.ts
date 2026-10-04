import { connectorBounds } from './render'
import { connectorType } from './connectors'
import { BOARD_ID } from './template'
import type { BoardTemplate } from './template'

/*
 * What is wrong or doubtful about a board template, for the board creator's checks list. Structural problems (an
 * unknown connector type, a pin count that does not fit) are errors; a connector off the board, two connectors on top of
 * each other and one sys pin on two connector pins are warnings.
 */

export interface BoardIssue {
  readonly severity: 'error' | 'warning'
  readonly message: string
  readonly connector?: string
  readonly pin?: number
}

export function validateBoard(board: BoardTemplate): BoardIssue[] {
  const issues: BoardIssue[] = []
  if (!BOARD_ID.test(board.id)) issues.push({ severity: 'error', message: 'The board ID must be lower case letters, digits and _, starting with a letter.' })
  const ids = new Set<string>()
  const used = new Map<string, string>()

  for (const connector of board.connectors) {
    if (ids.has(connector.id)) issues.push({ severity: 'error', message: `Connector ID ${connector.id} is used twice.`, connector: connector.id })
    ids.add(connector.id)
    const type = connectorType(connector.type)
    if (!type) {
      issues.push({ severity: 'error', message: `${connector.id}: the connector type ${connector.type} does not exist.`, connector: connector.id })
      continue
    }
    if (connector.pins.length !== type.pins.length) issues.push({ severity: 'error', message: `${connector.id}: ${type.label} has ${type.pins.length} pins, the template lists ${connector.pins.length}.`, connector: connector.id })
    const bounds = connectorBounds(connector)
    if (bounds.left < 0 || bounds.top < 0 || bounds.right > board.width || bounds.bottom > board.height) {
      issues.push({ severity: 'warning', message: `${connector.label || connector.id} reaches outside the board.`, connector: connector.id })
    }
    connector.pins.forEach((pin, index) => {
      if (pin.bind.type !== 'sys') return
      const key = `${pin.bind.device}.${pin.bind.pin}`
      const first = used.get(key)
      if (first) issues.push({ severity: 'warning', message: `Sys pin ${key} is on ${first} and on ${connector.label || connector.id} pin ${index + 1}.`, connector: connector.id, pin: index + 1 })
      else used.set(key, `${connector.label || connector.id} pin ${index + 1}`)
    })
  }

  for (let a = 0; a < board.connectors.length; a++) {
    for (let b = a + 1; b < board.connectors.length; b++) {
      const p = connectorBounds(board.connectors[a])
      const q = connectorBounds(board.connectors[b])
      if (p.left < q.right - 0.01 && q.left < p.right - 0.01 && p.top < q.bottom - 0.01 && q.top < p.bottom - 0.01) {
        issues.push({ severity: 'warning', message: `${board.connectors[a].label || board.connectors[a].id} and ${board.connectors[b].label || board.connectors[b].id} overlap.`, connector: board.connectors[b].id })
      }
    }
  }
  return issues
}
