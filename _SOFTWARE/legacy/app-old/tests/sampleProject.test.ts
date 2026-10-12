import { describe, expect, it } from 'vitest'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../src/domain/descriptors'
import { boardDefaultSettings, runitSettingsIds } from '../src/domain/upload'
import { buildStoredCode, decodeStoredCode } from '../src/domain/storedCode'
import { sampleProject } from '../src/sampleProject'

describe('the sample program', () => {
  it('builds and comes back from its own code', () => {
    const ctx = { vm: runitVmCatalog(), commands: runitCommandCatalog(), layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: runitDeviceCatalog() }
    const defaults = boardDefaultSettings()
    const code = buildStoredCode({ project: sampleProject(), settings: defaults, boardDefaults: defaults }, ctx)
    expect(code.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    const recovered = decodeStoredCode(code.steps.map((step) => step.frame), defaults, ctx, 'Sample')
    expect(recovered.blocks).toHaveLength(13)
  })
})
