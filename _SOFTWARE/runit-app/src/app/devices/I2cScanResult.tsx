import { Badge } from '@ui/components'
import { addressText } from './i2cScan'

/** What a bus scan found: how many, and each address with the name of the device the board keeps there, when it knows one. */
export function I2cScanResult({ addresses, nameAt }: { addresses: readonly number[]; nameAt: (address: number) => string | undefined }) {
  if (addresses.length === 0) return <p className="devices-answer">Nothing answered on this bus.</p>
  return (
    <div className="devices-answer" role="status">
      <p className="devices-scan-count">{addresses.length === 1 ? '1 device answered' : `${addresses.length} devices answered`}</p>
      <ul className="devices-scan-list">
        {addresses.map((address) => {
          const name = nameAt(address)
          return (
            <li key={address}>
              <Badge tone={name ? 'success' : 'neutral'} size="compact">{addressText(address)}</Badge>
              <span className={name ? undefined : 'devices-scan-unknown'}>{name ?? 'unknown'}</span>
            </li>
          )
        })}
      </ul>
    </div>
  )
}
