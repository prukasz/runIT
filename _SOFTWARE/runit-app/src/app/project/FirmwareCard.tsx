import { useCallback, useEffect, useMemo, useState } from 'react'
import { HardDriveUpload } from 'lucide-react'
import { bundledEnums, bundledServiceCatalog, otaInfo, serviceCall, writeFirmware } from '@backend'
import type { OtaInfo, RunitLink } from '@backend'
import { Button, Card, FieldError, FormGrid, FormRow, TextField } from '@ui/components'

const OTA_STATE_PENDING = 2 // sys_ota_state_e

/** The alias of a `sys_ota_state_e` value (`Not confirmed`), from the published enums. */
const stateAlias = (value: number): string => bundledEnums().sys_ota_state_e?.members.find((m) => m.value === value)?.alias ?? String(value)

/**
 * A new firmware over the link (SYS_OTA.MD): a file of the build (`runIT.bin`) into the board's other slot, then a restart. The
 * app confirms it once the board is back; if the board can't get that far, its next restart goes back to the old one.
 */
export function FirmwareCard({ link, connected }: { link: RunitLink; connected: boolean }) {
  const catalog = useMemo(() => bundledServiceCatalog(), [])
  const [info, setInfo] = useState<OtaInfo | undefined>()
  const [file, setFile] = useState<File | undefined>()
  const [fileKey, setFileKey] = useState(0) // a new input after an update: the same file can be chosen again
  const [busy, setBusy] = useState<string | undefined>()
  const [done, setDone] = useState<string | undefined>()
  const [error, setError] = useState<string | undefined>()

  const refresh = useCallback(async () => {
    try {
      setInfo(await otaInfo(link, catalog))
    } catch (failure) {
      setInfo(undefined)
      setError(failure instanceof Error ? failure.message : String(failure))
    }
  }, [link, catalog])

  useEffect(() => {
    setInfo(undefined)
    if (connected) void refresh()
  }, [connected, refresh])

  // The app confirms a new firmware when the board connects: read again until that shows here.
  useEffect(() => {
    if (!connected || info?.state !== OTA_STATE_PENDING) return
    const timer = window.setTimeout(() => void refresh(), 1000)
    return () => window.clearTimeout(timer)
  }, [connected, info, refresh])

  const update = useCallback(async () => {
    if (!file) return
    setError(undefined)
    setDone(undefined)
    try {
      setBusy('Writing… 0%')
      const image = new Uint8Array(await file.arrayBuffer())
      await writeFirmware(link, catalog, image, { onProgress: (written, total) => setBusy(`Writing… ${Math.round((written / total) * 100)}%`) })
      setBusy('Restarting…')
      await link.request(serviceCall(catalog, 'SYS', 'RESTART').request, { label: 'SYS RESTART' })
      setDone(`${file.name} written (${image.length} bytes). The board restarts and starts it; the app keeps it once the board is back.`)
      setFile(undefined)
      setFileKey((key) => key + 1)
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure))
    } finally {
      setBusy(undefined)
    }
  }, [file, link, catalog])

  if (!connected) return <Card title="Firmware" subhead="Connect the board to update its firmware." />
  return (
    <Card title="Firmware" subhead="A new firmware file (runIT.bin) over this link. The program on the board is stopped and unloaded first.">
      <FormGrid>
        <FormRow label="Running from">
          <span>{info ? `Slot ${info.slot}` : '…'}</span>
        </FormRow>
        <FormRow label="State">
          <span>{info ? stateAlias(info.state) : '…'}</span>
        </FormRow>
        <FormRow label="File">
          <TextField key={fileKey} type="file" accept=".bin" aria-label="Firmware file" disabled={busy !== undefined} onChange={(e) => setFile(e.target.files?.[0])} />
        </FormRow>
      </FormGrid>
      {busy && <p className="project-note">{busy}</p>}
      {done && <p className="project-note">{done}</p>}
      <FieldError>{error}</FieldError>
      <div className="project-actions">
        <Button variant="primary" disabled={!file || busy !== undefined} onClick={() => void update()}><HardDriveUpload aria-hidden="true" />Update firmware</Button>
      </div>
    </Card>
  )
}
